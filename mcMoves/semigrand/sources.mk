mcMd_modules_micelle_mcMoves_semigrand_=

ifdef SIMP_BOND
mcMd_modules_micelle_mcMoves_semigrand_+= \
    mcMd/modules/micelle/mcMoves/semigrand/UmbrellaSamplingSemigrandMove.cpp \
    mcMd/modules/micelle/mcMoves/semigrand/WangLandauSemigrandMove.cpp
endif

mcMd_modules_micelle_mcMoves_semigrand_SRCS=\
     $(addprefix $(SRC_DIR)/, $(mcMd_modules_micelle_mcMoves_semigrand_))
mcMd_modules_micelle_mcMoves_semigrand_OBJS=\
     $(addprefix $(BLD_DIR)/, $(mcMd_modules_micelle_mcMoves_semigrand_:.cpp=.o))

