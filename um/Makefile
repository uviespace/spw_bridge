# Build rules for the SPW_BRIDGE user manual, mirroring shared/doc.mk but
# keeping the generated PDF inside the repository tree.

TEX := usermanual.tex
JOB := usermanual
OUTPUT := SPW_BRIDGE-UVIE-UM-001.pdf

DOCS_DIR := .
OUTPUT_PATH := $(DOCS_DIR)/$(OUTPUT)

# FORCE is an unconditionally out-of-date prerequisite, so the recipe always
# runs a full XeLaTeX + makeglossaries + biber pass regardless of file mtimes.
.PHONY: all clean FORCE

all: $(OUTPUT_PATH)

FORCE:

$(OUTPUT_PATH): $(TEX) FORCE
	rm -f $(OUTPUT_PATH)
	xelatex $(TEX)
	makeglossaries $(JOB)
	biber $(JOB)
	xelatex $(TEX)
	xelatex $(TEX)
	xelatex $(TEX)
	mv $(JOB).pdf $(OUTPUT_PATH)

clean:
	find . -maxdepth 1 -type f -name '$(JOB).*' ! -name '$(TEX)' -delete