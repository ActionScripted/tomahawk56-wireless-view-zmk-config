.DEFAULT_GOAL := help
.PHONY: setup init update build build-left build-right build-reset test clean distclean flash flash-left flash-right flash-reset live-keymap clear-pinned-keymap lint lint-shell lint-yaml hooks help

help: ## Show available commands
	@echo "Usage: make [command]"
	@awk '/^# ---$$/ { \
		getline; \
		if ($$0 ~ /^# / && $$0 !~ /^# IGNORE/) { \
			section = substr($$0, 3); \
			print "\n" section; \
		} \
	} \
	/^[a-zA-Z_-]+:.*?## / { \
		if (section && section != "IGNORE") { \
			split($$0, parts, ":"); \
			comment = substr($$0, index($$0, "## ") + 3); \
			printf "  %-18s %s\n", parts[1], comment; \
		} \
	}' $(MAKEFILE_LIST)

RUN := docker compose run --rm zmk

# ---
# SETUP

setup: ## One-time bootstrap: mise install, west init, git hooks (run this first)
	@mise install
	@$(MAKE) init
	@$(MAKE) hooks

# ---
# BUILD

init: ## west init + update (safe to rerun, e.g. after changing west.yml)
	@$(RUN) ./scripts/build.sh init

build: ## Build left, right, and reset -> artifacts/*.uf2
	@$(RUN) ./scripts/build.sh all

build-left: ## Build just the left half
	@$(RUN) ./scripts/build.sh left

build-right: ## Build just the right half
	@$(RUN) ./scripts/build.sh right

build-reset: ## Build just the settings_reset image
	@$(RUN) ./scripts/build.sh reset

update: ## west update (pull latest pinned zmk/module revisions)
	@$(RUN) ./scripts/build.sh update

test: ## Run behavior tests (tests/) on ZMK's native simulator
	@$(RUN) ./scripts/build.sh test

clean: ## Remove build/test output and UF2s (keep downloaded dependencies)
	@rm -rf .build/firmware .build/tests .build/tmp artifacts

distclean: ## Full reset: remove dependencies and all generated output
	@rm -rf .build artifacts .west zmk zmk-rgbled-widget modules optional zephyr build

# ---
# FLASH (host, not Docker - needs to see the USB drive)

flash: ## Flash both halves in sequence (prompts for each)
	@./scripts/flash.sh all

flash-left: ## Flash just the left half
	@./scripts/flash.sh left

flash-right: ## Flash just the right half
	@./scripts/flash.sh right

flash-reset: ## Flash the settings_reset image (clears saved Studio keymap + BT pairings)
	@./scripts/flash.sh reset

# ---
# DIAGNOSE (host, not Docker - needs the USB serial port)

live-keymap: ## Show what keymap the board is REALLY running vs. the build (plug in the left half)
	@./scripts/live-keymap.py

clear-pinned-keymap: ## Delete Studio-saved bindings so the built keymap wins again (keeps BT pairings)
	@./scripts/live-keymap.py --clear

# ---
# LINT (mise tasks from .mise.toml; lefthook's pre-commit hook calls them too)

hooks: ## Install git hooks (included in `make setup`)
	@lefthook install

lint: ## Run all linters
	@mise run lint

lint-shell: ## shellcheck + shfmt on scripts/*.sh
	@mise run lint-shell

lint-yaml: ## yamllint on compose.yaml, build.yaml, lefthook.yml, and workflow files
	@mise run lint-yaml
