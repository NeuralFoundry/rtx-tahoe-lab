#pragma once
#include <cstdint>
struct ColorCase255 {const char *name; uint32_t red,green;};
static const ColorCase255 cases255[16]={
 {"endpoints",0x00000000u,0x3f800000u},
 {"half",0x3f000000u,0x3f000000u},
 {"half_neighbors",0x3effffffu,0x3f000001u},
 {"quarters",0x3e800000u,0x3f400000u},
 {"threshold65_50_55",0x3e838384u,0x3e839d37u},
 {"threshold65_56_60",0x3e83a25bu,0x3e83b6eau},
 {"threshold127_49_51",0x3efffadcu,0x3f000292u},
 {"threshold127_55_56",0x3f000cdau,0x3f000f6cu},
 {"exact_levels",0x3b808081u,0x3f7efeffu},
 {"clamp",0xbc23d70au,0x3f8147aeu},
 {"tenths",0x3dcccccdu,0x3f666666u},
 {"even_ties",0x3efdfdfeu,0x3f010101u},
 {"low_ties",0x3bc0c0c1u,0x3c20a0a1u},
 {"end_ties",0x3b008081u,0x3f7f7f7fu},
 {"quarter_neighbors",0x3e7fffffu,0x3e800001u},
 {"threequarter_neighbors",0x3f3fffffu,0x3f400001u},
};
