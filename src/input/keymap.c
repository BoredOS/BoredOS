#include "keymap.h"

typedef struct {
    const char *name;
    const char normal[128];
    const char shift[128];
} keymap_t;

static const keymap_t qwerty = {
    "QWERTY",
    {
        0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
        '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
        0,'a','s','d','f','g','h','j','k','l',';','\'','`',
        0,'\\','z','x','c','v','b','n','m',',','.','/',0,
        '*',0,' ',0
    },
    {
        0,27,'!','@','#','$','%','^','&','*','(',')','_','+','\b',
        '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
        0,'A','S','D','F','G','H','J','K','L',':','"','~',
        0,'|','Z','X','C','V','B','N','M','<','>','?',0,
        '*',0,' ',0
    }
};

static const keymap_t azerty = {
    "AZERTY",
    {
        0,27,'&','é','"','\'','(','-','è','_','ç','à',')','=','\b',
        '\t','a','z','e','r','t','y','u','i','o','p','^','$','\n',
        0,'q','s','d','f','g','h','j','k','l','m','ù','²',
        0,'*','w','x','c','v','b','n',',',';','.',':','!',0,
        '*',0,' ',0
    },
    {
        0,27,'1','2','3','4','5','6','7','8','9','0','°','+','\b',
        '\t','A','Z','E','R','T','Y','U','I','O','P','¨','£','\n',
        0,'Q','S','D','F','G','H','J','K','L','M','%','~',
        0,'µ','W','X','C','V','B','N','?','.','/','§',0,
        '*',0,' ',0
    }
};

static const keymap_t *current = &qwerty;

void keymap_init(void) {
    current = &qwerty;
}

void keymap_set_current(keymap_id_t id) {
    switch (id) {
        case KEYMAP_AZERTY: current = &azerty; break;
        case KEYMAP_QWERTY:
        default: current = &qwerty; break;
    }
}

keymap_id_t keymap_get_current(void) {
    return (current == &azerty) ? KEYMAP_AZERTY : KEYMAP_QWERTY;
}

char keymap_translate(uint8_t sc, bool shift, bool caps) {
    if (sc >= 128) return 0;

    char c = shift ? current->shift[sc] : current->normal[sc];

    if (caps && c >= 'a' && c <= 'z') {
        c = c - 'a' + 'A';
    }

    return c;
}
