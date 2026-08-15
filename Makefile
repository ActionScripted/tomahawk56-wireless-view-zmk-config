.DEFAULT_GOAL := help
.PHONY: setup init update build build-left build-right build-reset keymap test clean distclean
.PHONY: flash flash-left flash-right flash-reset live-keymap clear-pinned-keymap
.PHONY: lint lint-c lint-python lint-shell lint-yaml
.PHONY: format format-c format-python format-shell hooks help

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
KEYMAP_DRAWER := mise exec -- keymap -c keymap_drawer.config.yaml
KEYMAP_LAYOUT := .build/west/zmk/app/dts/layouts/mityaliu/tomahawk56.dtsi
KEYMAP_BUILD := .build/keymap

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

build-left: ## Build the left half
	@$(RUN) ./scripts/build.sh left

build-right: ## Build the right half
	@$(RUN) ./scripts/build.sh right

build-reset: ## Build the settings_reset image
	@$(RUN) ./scripts/build.sh reset

update: ## west update (pull latest pinned zmk/module revisions)
	@$(RUN) ./scripts/build.sh update

test: ## Run behavior tests (tests/) on ZMK's native simulator
	@$(RUN) ./scripts/build.sh test "$(TEST)"

clean: ## Remove build/test output and UF2s (keep downloaded dependencies)
	@rm -rf .build/firmware .build/keymap .build/tests .build/tmp artifacts

distclean: ## Full reset: remove dependencies and all generated output
	@rm -rf .build artifacts .west zmk zmk-rgbled-widget modules optional zephyr build

# ---
# DOCUMENTATION

keymap: ## Generate docs/keymap.svg from the ZMK keymap
	@test -f "$(KEYMAP_LAYOUT)" || { \
		echo "Missing $(KEYMAP_LAYOUT); run 'make init' first." >&2; exit 1; \
	}
	@mkdir -p $(KEYMAP_BUILD) docs
	@$(KEYMAP_DRAWER) parse -z config/tomahawk56.keymap -c 12 \
		-o $(KEYMAP_BUILD)/tomahawk56.yaml
	@./scripts/keymap-colors.py
	@$(KEYMAP_DRAWER) draw $(KEYMAP_BUILD)/tomahawk56.yaml \
		$(KEYMAP_BUILD)/colors.yaml -d $(KEYMAP_LAYOUT) -o docs/keymap.svg

# ---
# FLASH (host, not Docker - needs to see the USB drive)

flash: ## Flash both halves in sequence (prompts for each)
	@./scripts/flash.sh all

flash-left: ## Flash the left half
	@./scripts/flash.sh left

flash-right: ## Flash the right half
	@./scripts/flash.sh right

flash-reset: ## Flash the settings_reset image (clears saved Studio keymap + BT pairings)
	@./scripts/flash.sh reset

# ---
# DIAGNOSE (host, not Docker - needs the USB serial port)

live-keymap: ## Compare the left half's live keymap with the build
	@./scripts/live-keymap.py

clear-pinned-keymap: ## Delete Studio-saved bindings so the built keymap wins again (keeps BT pairings)
	@./scripts/live-keymap.py --clear

# ---
# LINT (mise tasks from .mise.toml; lefthook's pre-commit hook calls them too)

hooks: ## Install git hooks (included in `make setup`)
	@lefthook install

lint: ## Run all linters
	@mise run lint

lint-c: ## clang-format check on config/src C sources and headers
	@mise run lint-c

lint-python: ## Ruff checks and formatting validation on scripts/*.py
	@mise run lint-python

lint-shell: ## shellcheck + shfmt on scripts/*.sh
	@mise run lint-shell

lint-yaml: ## yamllint on repository YAML configuration and workflows
	@mise run lint-yaml

# ---
# FORMAT

format: ## Format repository-owned C, Python, and shell code
	@mise run format

format-c: ## Format config/src C sources and headers
	@mise run format-c

format-python: ## Format and sort imports in scripts/*.py
	@mise run format-python

format-shell: ## Format scripts/*.sh
	@mise run format-shell
