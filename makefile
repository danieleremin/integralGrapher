# ----------------------------
# Makefile Options
# ----------------------------

NAME = INTGRPH
ICON = icon.png
DESCRIPTION = "Integral grapher"
COMPRESSED = NO
HAS_PRINTF = NO

CFLAGS = -Wall -Wextra -Oz
CXXFLAGS = -Wall -Wextra -Oz

# ----------------------------

include $(shell cedev-config --makefile)
