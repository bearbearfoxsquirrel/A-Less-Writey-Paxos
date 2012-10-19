CFLAGS 		= -Wall -g -Wshadow
CC			= gcc

# Do not modify the following
OBJS		= $(subst .c,.o,$(SRCS))
CPPFLAGS	= -I../include/ -I../ -I$(BDB_DIR)/include -I$(LEV_DIR)/include
DEPS 		= $(patsubst %.c,.deps/%.d,$(SRCS))
SNAME		:= $(shell uname)

all: $(OBJS) $(LIBS) $(PROGRAMS) aux

-include $(DEPS)
	
%: %.o
	@echo "> $@ <"
	$(QUIET) $(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	@echo "> $@ <"
	$(QUIET) test -d .deps || mkdir .deps;
	$(QUIET) $(CC) $(CPPFLAGS) $(CFLAGS) $< -MT $@ -MM -MP -MF .deps/$*.d
	$(QUIET) $(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	$(QUIET) rm -f $(PROGRAMS) $(LIBS) $(OBJS) $(AUX_FILES)
	$(QUIET) rm -drf  .deps
	
.PHONY: clean $(AUX_ALL) aux