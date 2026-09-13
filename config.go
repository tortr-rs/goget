package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

const defaultConfig = "pkg.repos{github.on gitlab.on codeberg.on}\n"

type useFlag struct {
	name string
	on   bool
}

type useBlock struct {
	pkg   string // "" for the global pkg.USE block
	flags []useFlag
}

type gogetConfig struct {
	githubOn, gitlabOn, codebergOn bool
	useBlocks                      []useBlock
}

func configDir() string {
	return filepath.Join(homeDir(), ".config/goget")
}

func configPath() string {
	return filepath.Join(configDir(), "goget.conf")
}

func dieConfig(format string, a ...any) {
	fmt.Fprintf(os.Stderr, "goget: config error: "+format+"\n", a...)
	os.Exit(1)
}

// parseFlagToken parses one whitespace-separated "<name>.on" /
// "<name>.off" token. Hard-errors on anything else, since this is a
// small fixed grammar and a typo should be loud.
func parseFlagToken(token string) (string, bool) {
	if strings.HasSuffix(token, ".on") && len(token) > 3 {
		return token[:len(token)-3], true
	}
	if strings.HasSuffix(token, ".off") && len(token) > 4 {
		return token[:len(token)-4], false
	}
	dieConfig("expected '<name>.on' or '<name>.off', got '%s'", token)
	return "", false // unreachable
}

func parseBlockBody(body string, isReposBlock bool, cfg *gogetConfig) []useFlag {
	var flags []useFlag
	for _, tok := range strings.Fields(body) {
		name, on := parseFlagToken(tok)
		if isReposBlock {
			switch name {
			case "github":
				cfg.githubOn = on
			case "gitlab":
				cfg.gitlabOn = on
			case "codeberg":
				cfg.codebergOn = on
			default:
				dieConfig("unrecognized host '%s' in pkg.repos{}", name)
			}
		} else {
			flags = append(flags, useFlag{name: name, on: on})
		}
	}
	return flags
}

func parseConfig(content string) *gogetConfig {
	cfg := &gogetConfig{githubOn: true, gitlabOn: true, codebergOn: true}

	p := content
	for {
		p = strings.TrimLeft(p, " \t\r\n")
		if p == "" {
			break
		}
		if p[0] == '#' {
			if idx := strings.IndexByte(p, '\n'); idx >= 0 {
				p = p[idx+1:]
			} else {
				p = ""
			}
			continue
		}

		if !strings.HasPrefix(p, "pkg.") {
			preview := p
			if len(preview) > 20 {
				preview = preview[:20]
			}
			dieConfig("expected a 'pkg.<key>{...}' block, found: '%s'", preview)
		}
		p = p[4:]

		keyEnd := strings.IndexAny(p, "{ \t\r\n")
		if keyEnd < 0 {
			dieConfig("expected '{' after 'pkg.'")
		}
		key := p[:keyEnd]
		p = p[keyEnd:]

		p = strings.TrimLeft(p, " \t\r\n")
		if p == "" || p[0] != '{' {
			dieConfig("expected '{' after 'pkg.%s'", key)
		}
		p = p[1:]

		closeIdx := strings.IndexByte(p, '}')
		if closeIdx < 0 {
			dieConfig("unterminated block 'pkg.%s{'", key)
		}
		body := p[:closeIdx]
		p = p[closeIdx+1:]

		switch {
		case key == "repos":
			parseBlockBody(body, true, cfg)
		case key == "USE" || strings.HasSuffix(key, ".USE"):
			pkg := ""
			if key != "USE" {
				pkg = key[:len(key)-4]
			}
			flags := parseBlockBody(body, false, cfg)
			cfg.useBlocks = append(cfg.useBlocks, useBlock{pkg: pkg, flags: flags})
		default:
			dieConfig("unrecognized config block key 'pkg.%s'", key)
		}
	}

	return cfg
}

// configLoad loads ~/.config/goget/goget.conf, creating it with
// all-hosts-enabled defaults if missing. Hard-errors on a malformed
// file or an unrecognized pkg.<key>{} block key.
func configLoad() *gogetConfig {
	content, err := os.ReadFile(configPath())
	if err != nil {
		if mkErr := mkdirP(configDir()); mkErr != nil {
			fmt.Fprintf(os.Stderr, "goget: could not create config directory: %v\n", mkErr)
			os.Exit(1)
		}
		if writeErr := os.WriteFile(configPath(), []byte(defaultConfig), 0644); writeErr != nil {
			fmt.Fprintf(os.Stderr, "goget: could not create config file: %v\n", writeErr)
			os.Exit(1)
		}
		content = []byte(defaultConfig)
	}
	return parseConfig(string(content))
}

func configHostEnabled(cfg *gogetConfig, host string) bool {
	switch host {
	case "github.com":
		return cfg.githubOn
	case "gitlab.com":
		return cfg.gitlabOn
	case "codeberg.org":
		return cfg.codebergOn
	default:
		return true // hosts outside the three goget.conf tracks are unaffected
	}
}

func configSetHostEnabled(cfg *gogetConfig, host string, enabled bool) {
	switch host {
	case "github.com":
		cfg.githubOn = enabled
	case "gitlab.com":
		cfg.gitlabOn = enabled
	case "codeberg.org":
		cfg.codebergOn = enabled
	}
}

func onOff(b bool) string {
	if b {
		return "on"
	}
	return "off"
}

func renderConfig(cfg *gogetConfig) string {
	var sb strings.Builder
	fmt.Fprintf(&sb, "pkg.repos{github.%s gitlab.%s codeberg.%s}\n",
		onOff(cfg.githubOn), onOff(cfg.gitlabOn), onOff(cfg.codebergOn))
	for _, b := range cfg.useBlocks {
		if b.pkg == "" {
			sb.WriteString("pkg.USE{")
		} else {
			fmt.Fprintf(&sb, "pkg.%s.USE{", b.pkg)
		}
		for i, f := range b.flags {
			if i > 0 {
				sb.WriteByte(' ')
			}
			fmt.Fprintf(&sb, "%s.%s", f.name, onOff(f.on))
		}
		sb.WriteString("}\n")
	}
	return sb.String()
}

// configSave rewrites the config file in canonical form from cfg's
// current state. This is a full regeneration (the parser doesn't track
// original formatting), so hand-added comments won't survive a save
// triggered by "Always allow this host".
func configSave(cfg *gogetConfig) error {
	return os.WriteFile(configPath(), []byte(renderConfig(cfg)), 0644)
}

// configUseFlag resolves a USE flag's effective value for a package: a
// pkg.<package>.USE{} override beats pkg.USE{} (global) beats
// default-on.
func configUseFlag(cfg *gogetConfig, pkg, flag string) bool {
	for _, b := range cfg.useBlocks {
		if b.pkg == pkg {
			for _, f := range b.flags {
				if f.name == flag {
					return f.on
				}
			}
		}
	}
	for _, b := range cfg.useBlocks {
		if b.pkg == "" {
			for _, f := range b.flags {
				if f.name == flag {
					return f.on
				}
			}
		}
	}
	return true // default-on
}

func configPrint(cfg *gogetConfig) {
	fmt.Print(renderConfig(cfg))
}

type flagMapping struct {
	flag        string
	cmakeOptions []string
}

var fastfetchMappings = []flagMapping{
	{"wayland", []string{"ENABLE_WAYLAND"}},
	{"x11", []string{"ENABLE_XCB_RANDR", "ENABLE_XRANDR"}},
	{"pulseaudio", []string{"ENABLE_PULSE"}},
}

var packageFlagTables = map[string][]flagMapping{
	"fastfetch": fastfetchMappings,
}

// configBuildUseCmakeArgs translates a package's effective USE flags
// into extra CMake -D arguments via a small hardcoded per-package flag
// table (fastfetch is the initial test case: wayland/x11/pulseaudio). A
// package absent from the table, or a flag with no mapping for it,
// contributes nothing.
func configBuildUseCmakeArgs(cfg *gogetConfig, pkg string) []string {
	table, ok := packageFlagTables[pkg]
	if !ok {
		return nil
	}
	var args []string
	for _, m := range table {
		on := configUseFlag(cfg, pkg, m.flag)
		for _, opt := range m.cmakeOptions {
			args = append(args, fmt.Sprintf("-D%s=%s", opt, strings.ToUpper(onOff(on))))
		}
	}
	return args
}
