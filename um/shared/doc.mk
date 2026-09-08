# Shared build rules for the controlled OS documents.
#
# Each document Makefile (Documentation/OS/<doc>/Makefile) defines:
#   TEX    - the LaTeX master file (e.g. usermanual.tex)
#   JOB    - the job name (e.g. usermanual)
#   OUTPUT - the destination PDF basename under Documentation/datapack/docs/
# then includes this fragment, which provides the all/clean rules.
#
# The generated PDF is written to ../../datapack/docs/OUTPUT (i.e.
# Documentation/datapack/docs/OUTPUT relative to a document subdirectory).
# The default target always removes an existing PDF at that destination and
# does a full rebuild, so the committed PDF is always refreshed and no stale
# output can linger.
#
# FORCE is an unconditionally out-of-date prerequisite, so the recipe always
# runs a full XeLaTeX + makeglossaries + biber pass regardless of file mtimes.

DOCS_DIR := ../../datapack/docs
OUTPUT_PATH := $(DOCS_DIR)/$(OUTPUT)

# Additional per-document files to remove on clean (e.g. generated
# requirements lists) may be listed here by the including Makefile.
EXTRA_CLEAN ?=

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
ifneq ($(strip $(EXTRA_CLEAN)),)
	rm -f $(EXTRA_CLEAN)
endif