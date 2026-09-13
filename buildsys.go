package main

import "path/filepath"

type buildsysKind int

const (
	buildNone buildsysKind = iota
	buildCMake
	buildMake
	buildAutotools
)

// buildsysDetect detects the build system in dir by checking for marker
// files, in priority order: CMakeLists.txt -> cmake; an executable
// "configure" script -> autotools; a Makefile -> make.
func buildsysDetect(dir string) buildsysKind {
	if pathExists(filepath.Join(dir, "CMakeLists.txt")) {
		return buildCMake
	}
	if pathIsExecutableFile(filepath.Join(dir, "configure")) {
		return buildAutotools
	}
	if pathExists(filepath.Join(dir, "Makefile")) || pathExists(filepath.Join(dir, "makefile")) {
		return buildMake
	}
	return buildNone
}

func buildsysName(kind buildsysKind) string {
	switch kind {
	case buildCMake:
		return "CMake"
	case buildMake:
		return "Make"
	case buildAutotools:
		return "Autotools"
	default:
		return "none"
	}
}

func buildCmakeImpl(dir string, extraFlags []string) int {
	builddir := filepath.Join(dir, "build")

	argv := append([]string{"cmake", "-S", dir, "-B", builddir}, extraFlags...)
	// Configure/compile output is captured behind a spinner instead of
	// flooding the terminal -- only shown in full on failure, when it's
	// the only useful diagnostic. The install step stays plain
	// passthrough: it needs sudo, and sudo's password prompt goes
	// straight to the controlling terminal, which would visually collide
	// with an animated spinner redrawing the same line.
	rc := runCommandSpinner("", argv, "configuring with CMake...", false)
	if rc != 0 {
		printErr("cmake configure failed")
		return rc
	}
	printOK("configured")

	rc = runCommandSpinner("", []string{"cmake", "--build", builddir}, "building", true)
	if rc != 0 {
		printErr("cmake build failed")
		return rc
	}
	printOK("compiled")

	printInfo("installing (sudo cmake --install)...")
	rc = runCommand("", []string{"sudo", "cmake", "--install", builddir})
	if rc != 0 {
		printErr("cmake --install failed")
	}
	return rc
}

func buildMakeImpl(dir string) int {
	rc := runCommandSpinner(dir, []string{"make"}, "building with make...", false)
	if rc != 0 {
		printErr("make failed")
		return rc
	}
	printOK("compiled")

	printInfo("installing (sudo make install)...")
	rc = runCommand(dir, []string{"sudo", "make", "install"})
	if rc != 0 {
		// Don't guess at a specific cause (missing install target, a file
		// the install step references being absent, a permissions
		// issue, ...) -- make already printed the real reason above;
		// repeating a wrong guess is worse than saying nothing.
		printErr("'make install' failed -- see output above for the reason.")
	}
	return rc
}

func buildAutotoolsImpl(dir string) int {
	rc := runCommandSpinner(dir, []string{"./configure"}, "running ./configure...", false)
	if rc != 0 {
		printErr("./configure failed")
		return rc
	}
	printOK("configured")
	return buildMakeImpl(dir)
}

// buildsysBuildAndInstall builds and installs the project in dir using
// the given build system. extraCmakeFlags are appended to the cmake
// configure step only; other build systems ignore them for now. Install
// steps run under sudo, since they write outside the user's home
// directory.
func buildsysBuildAndInstall(kind buildsysKind, dir string, extraCmakeFlags []string) int {
	switch kind {
	case buildCMake:
		return buildCmakeImpl(dir, extraCmakeFlags)
	case buildMake:
		return buildMakeImpl(dir)
	case buildAutotools:
		return buildAutotoolsImpl(dir)
	default:
		printErr("internal error: no build system to run")
		return -1
	}
}
