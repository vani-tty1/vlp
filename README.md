# vlp
 
A command-line tool that parses `/var/log/pacman.log` (including its
rotated `.log.1` / `.log.1.gz` predecessor) and shows one of four views:
 
- **`-i, --installed`** — show recent packages currently installed according to the log
- **`-c, --show-commands`** — recent user-initiated `pacman -S ...` / sync commands
- **`-u, --upgraded`** — recent package upgrades
- **`-s, --show-hist`** — recent transactions, with installed/removed/upgraded/downgraded/reinstalled
packages grouped under the transaction (and triggering command) they occurred in


A mode flag is required; an optional `n` argument limits how many matching
log lines are considered (default: 20). For `-s`, `n` instead limits how
many of the most recent transactions are shown (also default: 20).


## Installation

[![Packaging status](https://repology.org/badge/vertical-allrepos/vlp.svg)](https://repology.org/project/vlp/versions)

vlp is available at the [AUR](https://aur.archlinux.org/packages/vlp), 
or you can install using the `install` target in the [Makefile](Makefile)

## Building
 
Requires `meson`, `ninja`, and `zlib`.
 
```
meson setup build
meson compile -C build
```

>NOTE: See the [Makefile](Makefile) for more build options
