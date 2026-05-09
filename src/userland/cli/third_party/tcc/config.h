#ifndef TCC_CONFIG_H
#define TCC_CONFIG_H

#define TCC_TARGET_X86_64
#define TARGETOS_BoredOS 1
#define TCC_VERSION "0.9.27"

#define CONFIG_TCCDIR "/usr/lib/tcc"
#define CONFIG_TCC_SYSINCLUDEPATHS "/usr/lib/tcc/include:/usr/include:/usr/local/include"
#define CONFIG_TCC_LIBPATHS "/usr/lib/tcc:/usr/local/lib:/usr/lib:/lib"
#define CONFIG_TCC_CRTPREFIX "/usr/lib"

#define CONFIG_TCC_STATIC 1
#define CONFIG_TCC_SEMLOCK 0

#endif
