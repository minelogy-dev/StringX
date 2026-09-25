/* The advanced layer (src/sx_adv.c, the sx_reg_* regex family) links
   PCRE2, and every test compiles the library sources directly, so
   pcre2-8 is attached to every test target here — via forge's
   reserved per-test DSL overrides (LANGUAGE / TOOLCHAIN expand inside
   each test target block; see build.h "test DSL default macros").
   Must be #defined before #include <build.h> (the guards). */
#define LANGUAGE                                                               \
  {                                                                            \
    set_language(C);                                                           \
    add_link_lib("pcre2-8");                                                   \
  }
#define TOOLCHAIN set_toolchain(GCC)
#include <build.h>

#define STR_IMPL(tok) #tok
#define STR(tok) STR_IMPL(tok)
#ifndef CONF_version
#define SX_VERSION "dev"
#else
#define SX_VERSION STR(CONF_version)
#endif

function(check) {
  parse_args(builtin_args);
  target("stringx") {
    add_sources_r("src");
    set_type(FORGE_SHARED_LIB);
    add_include_path("include");
    add_link_lib("pcre2-8");
    set_visibility(HIDDEN);
    set_optimization(OPT_AGGRESSIVE);
    set_standard("c23");
    set_output_path("build");
    add(COMPILER_OPTIONS, 5, "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-fanalyzer");
    set_output_intermediate_path("build/exe-obj");
    return compile();
  }
}

function(build) {
  if (os_mkdir_r("build/output/include") != 0)
    return -1;
  {
    int n = 0;
    char **names = os_listdir("include", &n);
    if (!names)
      return -1;
    for (int i = 0; i < n; i++) {
      if (!strstr(names[i], ".h"))
        continue;
      char *src = os_path_join("include", names[i]);
      char *dst = os_path_join("build/output/include", names[i]);
      int r = (!src || !dst) ? -1 : os_copy_file(src, dst);
      free(src);
      free(dst);
      if (r != 0) {
        for (int j = 0; j < n; j++)
          free(names[j]);
        free(names);
        return -1;
      }
    }
    for (int i = 0; i < n; i++)
      free(names[i]);
    free(names);
  }
  target("stringx") {
    add_sources_r("src");
    set_type(FORGE_SHARED_LIB);
    add_include_path("include");
    add_link_lib("pcre2-8");
    set_visibility(HIDDEN);
    set_optimization(OPT_AGGRESSIVE);
    set_standard("c23");
    set_output_path("build");
    set_output_intermediate_path("build/exe-obj");
    return compile();
  }
  return 0;
}

/* ---------------- install / gen_deb (Linux only) ----------------
 * Same pattern as the forge project itself: install copies the
 * artifacts (shared library, headers, license/notices) under a
 * prefix; gen_deb stages the same tree into a Debian package built
 * with tar/ar (no dpkg-deb dependency),
 * build/deb/libstringx_<ver>_<arch>.deb.
 * Effective only on Linux (guarded by the LINUX compile-time macro
 * from build.h); elsewhere the function bodies are a runtime error +
 * return -1 (deliberately not #error, so build.c still compiles
 * everywhere). */

#if LINUX

/* Recursive copy of every entry under src_dir into dst_dir
   (os_copy_file handles files only).  Used by install_to. */
static int copy_dir_r(const char *src_dir, const char *dst_dir) {
  if (os_mkdir_r(dst_dir) != 0)
    return -1;
  int n = 0;
  char **names = os_listdir(src_dir, &n);
  if (!names)
    return -1;
  int ret = 0;
  for (int i = 0; i < n && ret == 0; i++) {
    char *s = os_path_join(src_dir, names[i]);
    char *d = os_path_join(dst_dir, names[i]);
    if (!s || !d) {
      ret = -1;
    } else if (os_path_is_dir(s)) {
      ret = copy_dir_r(s, d);
    } else if (os_copy_file(s, d) != 0) {
      ret = -1;
    }
    free(s);
    free(d);
  }
  for (int i = 0; i < n; i++)
    free(names[i]);
  free(names);
  return ret;
}

/* Copy the built artifacts to the system paths under prefix (shared
   by install and gen_deb's data root): build/output/libstringx.so ->
   <prefix>/lib, the synced headers (build/output/include) ->
   <prefix>/include, and LICENSE + THIRD_PARTY_NOTICES.md ->
   <prefix>/share/doc/libstringx (LICENSE also as the Debian `copyright`).
   Returns 0 on success. */
static int install_to(const char *prefix) {
  int ret = 0;
  char *lib = os_path_join(prefix, "lib");
  char *inc = os_path_join(prefix, "include");
  char *doc = os_path_join(prefix, "share/doc/libstringx");
  if (!lib || !inc || !doc || os_mkdir_r(lib) != 0 || os_mkdir_r(inc) != 0 ||
      os_mkdir_r(doc) != 0) {
    free(lib);
    free(inc);
    free(doc);
    return -1;
  }

  char *dst = os_path_join(lib, "libstringx.so");
  if (os_copy_file("build/output/libstringx.so", dst) != 0)
    ret = -1;
  free(dst);

  if (copy_dir_r("build/output/include", inc) != 0)
    ret = -1;

  char *f = os_path_join(doc, "LICENSE");
  if (os_copy_file("LICENSE", f) != 0)
    ret = -1;
  free(f);
  f = os_path_join(doc, "THIRD_PARTY_NOTICES.md");
  if (os_copy_file("THIRD_PARTY_NOTICES.md", f) != 0)
    ret = -1;
  free(f);
  f = os_path_join(doc, "copyright"); /* Debian's conventional name */
  if (os_copy_file("LICENSE", f) != 0)
    ret = -1;
  free(f);

  /* pkg-config metadata: <prefix>/lib/pkgconfig/libstringx.pc.  The
     prefix is baked in at staging time, so the same installer serves
     dpkg (/usr) and plain-prefix installs (/usr/local, ~/.local …).
     Version comes from build.conf (SX_VERSION), leading v stripped. */
  {
    const char *v = SX_VERSION;
    if (*v == 'v')
      v++;
    char *pcdir = os_path_join(lib, "pkgconfig");
    char *pc = os_path_join(pcdir, "libstringx.pc");
    if (!pcdir || !pc || os_mkdir_r(pcdir) != 0) {
      ret = -1;
    } else {
      FILE *f = fopen(pc, "w");
      if (!f)
        ret = -1;
      else {
        fprintf(f,
                "prefix=%s\n"
                "exec_prefix=${prefix}\n"
                "libdir=${prefix}/lib\n"
                "includedir=${prefix}/include\n"
                "\n"
                "Name: libstringx\n"
                "Description: String X - a small C string library with a "
                "single-header API and a byte-exact, zero-copy view model\n"
                "Version: %s\n"
                "Libs: -L${libdir} -lstringx\n"
                "Cflags: -I${includedir}\n",
                prefix, v);
        fclose(f);
      }
    }
    free(pcdir);
    free(pc);
  }

  free(lib);
  free(inc);
  free(doc);
  return ret;
}

/* uname -m -> Debian architecture name: x86_64->amd64,
   aarch64->arm64; anything else is kept as-is */
static const char *deb_arch(void) {
  static char buf[64];
  sx_t *out = os_shell((char *[]){"uname", "-m", NULL});
  if (!out)
    return "amd64";
  char *s = sx_buf_mut(out);
  size_t len = strlen(s);
  while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r'))
    s[--len] = '\0';
  const char *m = s;
  if (strcmp(m, "x86_64") == 0)
    m = "amd64";
  else if (strcmp(m, "aarch64") == 0)
    m = "arm64";
  snprintf(buf, sizeof(buf), "%s", m);
  sx_free(out);
  return buf;
}

/* Packaging helper: tar/ar run directly via argv (status captured);
   returns -1 on failure */
static int run_quiet(char **argv) {
  int st = -1;
  sx_free(os_execute_capture_all_status(argv[0], argv, "", NULL, &st));
  return st == 0 ? 0 : -1;
}
#endif /* LINUX */

function(install) {
  parse_args();
  const char *prefix =
      (argc > 1 && argv[1] && *argv[1]) ? argv[1] : "/usr/local";
#if LINUX
  /* Ensure the artifacts (including the header sync) are fresh first */
  int r = function_build(argc, argv);
  if (r != 0)
    return r;
  r = install_to(prefix);
  if (r != 0)
    fprintf(stderr,
            "forge: install to %s failed\n"
            "  (check write permission, or pass a prefix you own, e.g. "
            "make install ~/.local)\n",
            prefix);
  return r;
#else
  (void)prefix;
  fprintf(stderr, "forge: install is only supported on Linux\n");
  return -1;
#endif
}

function(gen_deb) {
  parse_args();
#if LINUX
  /* Version: SX_VERSION from build.conf (single source of truth),
     leading v stripped ("v0.5" -> "0.5") */
  const char *v = SX_VERSION;
  if (*v == 'v')
    v++;
  char ver[64];
  snprintf(ver, sizeof(ver), "%s", v);
  const char *arch = deb_arch();

  int ret = function_build(argc, argv); /* ensure artifacts are fresh first */
  if (ret != 0)
    return ret;

  /* Layout:
       build/deb/
         debian-binary  data/  control-dir/control
         control.tar.gz data.tar.gz  libstringx_<ver>_<arch>.deb */
  char *work = os_path_join("build", "deb");
  char *data = os_path_join(work, "data");
  char *cdir = os_path_join(work, "control-dir");
  if (!work || !data || !cdir) {
    free(work);
    free(data);
    free(cdir);
    return -1;
  }
  os_remove_r(work);
  if (os_mkdir_r(data) != 0 || os_mkdir_r(cdir) != 0) {
    free(work);
    free(data);
    free(cdir);
    return -1;
  }

  /* Data root = reuse install's copy logic.  The deb data tree
     mirrors the filesystem root: packages install under /usr (FHS),
     so the copy root is <data>/usr — lib/libstringx.so, the headers,
     share/doc/libstringx (LICENSE, notices, copyright). */
  char *data_usr = os_path_join(data, "usr");
  if (!data_usr) {
    ret = -1;
    goto cleanup;
  }
  if (install_to(data_usr) != 0) {
    free(data_usr);
    free(work);
    free(data);
    free(cdir);
    return -1;
  }
  free(data_usr);

  char *deb_bin = os_path_join(work, "debian-binary");
  char *ctl = os_path_join(cdir, "control");
  char *cgz = os_path_join(work, "control.tar.gz");
  char *dgz = os_path_join(work, "data.tar.gz");
  size_t deblen = strlen(work) + strlen(ver) + strlen(arch) + 28;
  char *deb = malloc(deblen);
  if (!deb_bin || !ctl || !cgz || !dgz || !deb) {
    ret = -1;
    goto cleanup;
  }
  snprintf(deb, deblen, "%s/libstringx_%s_%s.deb", work, ver, arch);

  FILE *f = fopen(deb_bin, "w");
  if (f) {
    fputs("2.0\n", f);
    fclose(f);
  } else {
    ret = -1;
  }
  f = fopen(ctl, "w");
  if (f) {
    fprintf(f,
            "Package: libstringx\n"
            "Version: %s\n"
            "Section: libs\n"
            "Priority: optional\n"
            "Architecture: %s\n"
            "Maintainer: Minelogy <minelogy-dev@users.noreply.github.com>\n"
            "Depends: libc6, libpcre2-8-0\n"
            "Description: libstringx - a small C string library with a single-\n"
            " header API and a byte-exact, zero-copy view model\n",
            ver, arch);
    fclose(f);
  } else {
    ret = -1;
  }

  if (ret == 0) {
    char *t1[] = {"tar", "-czf", cgz, "-C", cdir, "control", NULL};
    char *t2[] = {"tar", "-czf", dgz, "-C", data, ".", NULL};
    char *ar[] = {"ar", "rcs", deb, deb_bin, cgz, dgz, NULL};
    if (run_quiet(t1) != 0 || run_quiet(t2) != 0 || run_quiet(ar) != 0)
      ret = -1;
    else
      printf("forge: %s\n", deb);
  }

cleanup:
  free(deb_bin);
  free(ctl);
  free(cgz);
  free(dgz);
  free(deb);
  free(work);
  free(data);
  free(cdir);
  return ret;
#else
  fprintf(stderr, "forge: gen_deb is only supported on Linux\n");
  return -1;
#endif
}

set_default(build);
default_test();