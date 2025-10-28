bin_PROGRAMS = ptype
ptype_CFLAGS  = -std=c11 -pedantic -Wall -Wextra -Werror \
				-Wno-unused -Wno-unused-parameter
ptype_LDFLAGS = $(NCURSES_LIBS) -lpanel
ptype_SOURCES = c.c \
				config_parser.c config_parser.h \
				confsetters.c confsetters.h \
				drw.c drw.h \
				loaders.c loaders.h \
				utils.c utils.h \
				def.h aart.h

