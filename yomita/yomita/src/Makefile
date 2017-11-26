
TARGET   = Yomita-by-clang
COMPILER = clang++

CFLAGS  = -std=c++14 -fno-exceptions -fno-rtti -Wextra -Ofast -MMD -MP -fpermissive
CFLAGS += -stdlib=libstdc++ -Wno-unused-parameter -flto -DIS_64BIT
LDFLAGS = -Wl,-s -lstdc++fs -lpthread -v

ifeq ($(findstring g++,$(COMPILER)),g++)
        OPENMP   = -fopenmp
        OPENMP_LDFLAGS =
endif
ifeq ($(findstring clang++,$(COMPILER)),clang++)
        OPENMP   = -fopenmp
        OPENMP_LDFLAGS = -lomp
endif

SOURCES  = main.cpp \
	benchmark.cpp \
	bitboard.cpp \
	board.cpp \
	book.cpp \
	byteboard.cpp \
	common.cpp \
	eval_kppt.cpp \
	eval_ppt.cpp \
	eval_pptp.cpp \
	eval_util.cpp \
	genmove.cpp \
	haffman.cpp \
	learn.cpp \
	movepick.cpp \
	pretty.cpp \
	progress.cpp \
	search.cpp \
	sfen_rw.cpp \
	test.cpp \
	thread.cpp \
	timeman.cpp \
	tt.cpp \
	usioption.cpp \
	usi.cpp


OBJDIR  = ../obj
OBJECTS  = $(addprefix $(OBJDIR)/, $(SOURCES:.cpp=.o))
DEPENDS  = $(OBJECTS:.o=.d)

$(TARGET): $(OBJECTS) $(LIBS)
	$(COMPILER) -o $@ $^ $(LDFLAGS) $(CFLAGS)

$(OBJDIR)/%.o: %.cpp
	@[ -d $(dir $@) ] || mkdir -p $(dir $@)
	$(COMPILER) $(CFLAGS) $(INCLUDE) -o $@ -c $<

all: clean $(TARGET)

avx2:
	$(MAKE) CFLAGS='$(CFLAGS) -DNDEBUG -DHAVE_BMI2 -DHAVE_SSE4 -mbmi2 -mavx2 -march=corei7-avx' LDFLAGS='$(LDFLAGS) $(LTOFLAGS)' $(TARGET)

sse42:
	$(MAKE) CFLAGS='$(CFLAGS) -DNDEBUG -DHAVE_SSE42 -msse4.2 -march=corei7' LDFLAGS='$(LDFLAGS) $(LTOFLAGS)' $(TARGET)

# sse:
# 	$(MAKE) CFLAGS='$(CFLAGS) -DNDEBUG -DHAVE_SSE2 -msse2 -march=core2' LDFLAGS='$(LDFLAGS) $(LTOFLAGS)' $(TARGET)_sse

learn:
	$(MAKE) CFLAGS='$(CFLAGS) -DHAVE_BMI2  -DHAVE_SSE4 -DLEARN -mbmi2 -mavx2 -march=corei7-avx' LDFLAGS='$(LDFLAGS) $(LTOFLAGS)' $(TARGET)

learn-sse42:
	$(MAKE) CFLAGS='$(CFLAGS) -DHAVE_SSE42 -DLEARN -mbmi2 -mavx2 -march=corei7-avx' LDFLAGS='$(LDFLAGS) $(LTOFLAGS)' $(TARGET)

clean:
	rm -f $(OBJECTS) $(DEPENDS) $(TARGET) ${OBJECTS:.o=.gcda}

-include $(DEPENDS)
