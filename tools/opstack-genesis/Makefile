# Copyright (c) FISCO-BCOS, Apache-2.0
# Build L2 genesis [alloc.N] INI fragments from the bcos-l2-contracts forge build.

# Path to the contracts package (relative to this dir by default).
CONTRACTS ?= ../../bcos-l2-contracts
# chain-config YAML (copy chain-config.template.yaml and edit).
CONFIG    ?= chain-config.yaml
# Output INI fragment.
OUT       ?= allocs.ini
PYTHON    ?= python3

.PHONY: all contracts allocs test clean

all: allocs

# Compile both the self-written predeploys and the vendored OP fork. The OP fork
# is a separate compile unit (solc 0.8.15) — see bcos-l2-contracts/README.md.
contracts:
	cd $(CONTRACTS) && forge build
	cd $(CONTRACTS) && forge build --contracts vendor/op-fork/src

# Generate the INI from the (already built) forge artifacts + chain config.
allocs:
	$(PYTHON) build-allocs.py --config $(CONFIG) --contracts $(CONTRACTS) --out $(OUT)

# Run the pytest suite (synthetic fixtures, no forge run required).
test:
	$(PYTHON) -m pytest test_build_allocs.py -v

clean:
	rm -f $(OUT)
