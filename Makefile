# Default target
.DEFAULT_GOAL := help

# Phony (non-file) targets
.PHONY: setup init update build build-left build-right build-reset clean flash flash-left flash-right flash-reset lint lint-shell lint-yaml hooks help

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

clean: ## Full reset: remove the west workspace + build output (re-run `make init` after)
	@rm -rf build artifacts .west zmk zmk-rgbled-widget modules optional zephyr

# ---
# FLASH (host, not Docker - needs to see the USB drive)

flash: ## Flash both halves in sequence (prompts for each)
	@./scripts/flash.sh all

flash-left: ## Flash just the left half
	@./scripts/flash.sh left

flash-right: ## Flash just the right half
	@./scripts/flash.sh right

flash-reset: ## Flash the settings_reset image (clears BT pairings)
	@./scripts/flash.sh reset

# ---
# LINT (mise tasks defined in .mise.toml; lefthook's pre-commit hook calls them directly too)

hooks: ## Install git hooks (included in `make setup`)
	@lefthook install

lint: ## Run all linters
	@mise run lint

lint-shell: ## shellcheck + shfmt on scripts/*.sh
	@mise run lint-shell

lint-yaml: ## yamllint on compose.yaml, build.yaml, and workflow files
	@mise run lint-yaml
