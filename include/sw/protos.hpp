#pragma once

#if !defined(_WIN64)
// The RVAs belong to stormworks64.exe (64 bit). On other targets the
// header provides nothing.
#else

#include <cstddef>
#include <cstdint>

#include "sw/runtime.hpp"
#include "sw/generated_symbols.hpp"

namespace sw::protos {


struct MmString {
    char* data;
    std::uint32_t size;
    std::uint32_t capacity;
};
static_assert(sizeof(MmString) == 16, "mm_string is 16 bytes");

// Result of AreaFn, as the game creates it at 0x577bc0:
// { pointer, capacity, start, count, free }.
struct AreaResult {
    void* data;
    std::uint32_t capacity;
    std::uint32_t start;
    std::uint32_t count;
    std::uint32_t free;
};

struct LuaValue {
    std::uint64_t value;
    std::uint8_t type;
};
static_assert(sizeof(LuaValue) == 16, "LuaValue is 16 bytes");
inline constexpr std::uint8_t kLuaTypeNumber = 3;
inline constexpr std::uint8_t kLuaTypeInteger = 19;
inline constexpr std::uint8_t kLuaTypeString = 4;

inline constexpr std::uint32_t kRvaOperatorNew = 0x95c6f8;
inline constexpr std::uint32_t kRvaStringReserve = 0x17ca10;
inline constexpr std::uint32_t kRvaFree = 0x9b6750;
inline constexpr std::uint32_t kRvaMemAllocAligned = 0x9b676c;

inline constexpr std::uint32_t kRvaStringAssign = 0x17b4a0;
inline constexpr std::uint32_t kRvaStringBufFree = 0x17b520;
inline constexpr std::uint32_t kRvaStringClear = 0x17b550;
inline constexpr std::uint32_t kRvaMmStringConcat = 0x17b9a0;

inline constexpr std::uint32_t kRvaMemset = 0x9a0b20;
inline constexpr std::uint32_t kRvaMemcpy = 0x9a0db0;
inline constexpr std::uint32_t kRvaMemCmp = 0x9a14e0;

inline constexpr std::uint32_t kRvaSinf = 0x9da8b0;
inline constexpr std::uint32_t kRvaCosf = 0x9d8810;
inline constexpr std::uint32_t kRvaSqrtDouble = 0x9dafa8;
inline constexpr std::uint32_t kRvaSqrtFloat = 0x9db074;
inline constexpr std::uint32_t kRvaMathSin = 0x9da340;
inline constexpr std::uint32_t kRvaMathCos = 0x9d8290;
inline constexpr std::uint32_t kRvaMathFloor = 0x9d93b8;
inline constexpr std::uint32_t kRvaMathFmod = 0x9d97c4;
inline constexpr std::uint32_t kRvaStrNCmp = 0x9a4ce0;
inline constexpr std::uint32_t kRvaArenaAlloc = 0x8ecdd0;
inline constexpr std::uint32_t kRvaCrtRand = 0x9b342c;
inline constexpr std::uint32_t kRvaCrtErrnoGet = 0x9b03b8;
inline constexpr std::uint32_t kRvaCrtHeapFree = 0x9c1294;
inline constexpr std::uint32_t kRvaMat3Mul = 0x1773a0;
inline constexpr std::uint32_t kRvaMat3Identity = 0x176860;
inline constexpr std::uint32_t kRvaMat3FromInts = 0x17a9a0;
inline constexpr std::uint32_t kRvaVec3Normalize = 0x175690;
inline constexpr std::uint32_t kRvaMat4FromDoubles = 0x179da0;
inline constexpr std::uint32_t kRvaMat4Invert = 0x178d90;
inline constexpr std::uint32_t kRvaMat4Identity = 0x178570;
inline constexpr std::uint32_t kRvaUiElementAttach = 0x248890;
inline constexpr std::uint32_t kRvaUiTextSet = 0x1889c0;
inline constexpr std::uint32_t kRvaLuaValueToInt = 0xbe590;
inline constexpr std::uint32_t kRvaLuaValueToFloat = 0xbe490;
inline constexpr std::uint32_t kRvaLuaNumberToString = 0xae280;

using OperatorNewFn = void* (*)(std::size_t bytes);
using FreeFn = void (*)(void* p);
using StringReserveFn = void (*)(MmString* str, std::uint32_t bytes);
using MemAllocAlignedFn = void* (*)(std::size_t bytes, std::size_t alignment);
using StringAssignFn = MmString* (*)(MmString* str, const char* cstr);
using StringBufFreeFn = void (*)(MmString* str);
using StringClearFn = void (*)(MmString* str);
using MemSetFn = void (*)(void* target, std::uint32_t value, std::size_t n);
using MemCopyFn = void (*)(void* target, const void* source, std::size_t n);
using SinfFn = float (*)(float x);
using CosfFn = float (*)(float x);
using SqrtDoubleFn = double (*)(double x);
using SqrtFloatFn = float (*)(float x);
using ArenaAllocFn = void* (*)(void* arena, std::size_t bytes);
using MemCmpFn = int (*)(const void* a, const void* b, std::size_t n);
using MathSinFn = double (*)(double x);
using MathCosFn = double (*)(double x);
using MathFloorFn = double (*)(double x);
using MathFmodFn = float (*)(float x, float y);
using StrNCmpFn = int (*)(const char* a, const char* b, std::size_t n);
using CrtRandFn = std::uint32_t (*)(void);
using Mat3MulFn = void (*)(float* result, const float* a, const float* b);
using Mat3IdentityFn = void (*)(float* m);
using Mat3FromIntsFn = void (*)(const int* source, float* target);
using MmStringConcatFn = MmString* (*)(const MmString* a, MmString* target, const MmString* b);
using CrtErrnoGetFn = std::uintptr_t (*)(void);
using CrtHeapFreeFn = void (*)(void* p);
using Vec3NormalizeFn = void (*)(float* v);
using Mat4FromDoublesFn = void (*)(const double* source, float* target);
using Mat4InvertFn = void (*)(const double* source, double* result);
using Mat4IdentityFn = void (*)(float* m);
using UiElementAttachFn = void (*)(void* parent, void* ctx, void* child);
using UiTextSetFn = void (*)(void* element, MmString* text);
using LuaValueToIntFn = int (*)(const LuaValue* value, double* target, int rounding);
using LuaValueToFloatFn = int (*)(const LuaValue* value, double* target);
using LuaNumberToStringFn = void (*)(std::uintptr_t lua_state, LuaValue* value);


inline constexpr std::uint32_t kRvaAddChild = 0x8f43e0;  // child onto the child list
inline constexpr std::uint32_t kRvaRectCtor = 0x245660;   // c_ui_element_rectangle
inline constexpr std::uint32_t kRvaRectVtable = 0xb9c338; // vtable of the rectangle
inline constexpr std::uint32_t kRvaTextCtor = 0x246ab0;   // c_ui_element_text
inline constexpr std::uint32_t kRvaTextVtable = 0xbac908; // vtable of the text
inline constexpr std::uint32_t kRvaTextSet = 0x246f00;    // replace the text
inline constexpr std::uint32_t kRvaButtonFactory = 0x263820;   // button factory
inline constexpr std::uint32_t kRvaPointButtonVtable = 0xaf2af0; // c_ui_element_point_button
inline constexpr std::uint32_t kRvaPointButtonCtor = 0x24a320;   // sets the vtable
inline constexpr std::uint32_t kRvaButtonRelease = 0x24aa20;     // button slot 18: release
inline constexpr std::uint32_t kRvaHitTest = 0x1dfe60;           // hit test (slot 7)
inline constexpr std::uint32_t kRvaVisibleCheck = 0x1df950;     // respects +0x1c
inline constexpr std::uint32_t kRvaAnchorCompute = 0x1e03d0;     // evaluate the anchor (+0x18)

// Fields of every c_ui_element (documented via construction 0x6ff5b0 and the mods):
inline constexpr std::size_t kOffX = 0x08;         // float
inline constexpr std::size_t kOffY = 0x0c;         // float
inline constexpr std::size_t kOffWidth = 0x10;     // float
inline constexpr std::size_t kOffHeight = 0x14;    // float
inline constexpr std::size_t kOffAnchor = 0x18;    // std::uint32_t, 0..8
inline constexpr std::size_t kOffVisible = 0x1c;   // std::uint8_t, 0 = do not draw/do not hit
inline constexpr std::size_t kOffParent = 0x20;    // Element*
inline constexpr std::size_t kOffChildLst = 0x30;  // child list (pass this address on)
inline constexpr std::size_t kRectSize = 0x60;     // sizeof(c_ui_element_rectangle)
inline constexpr std::size_t kTextSize = 0xC8;     // sizeof(c_ui_element_text) = 200 bytes

// Button fields:
inline constexpr std::size_t kOffAction = 0x90;    // action ID (0 = none)
inline constexpr std::size_t kOffCallback = 0xd8;  // callback on release
inline constexpr std::size_t kOffCbUser = 0xe0;    // user pointer with it

// vtable slots of every UI element:
inline constexpr std::size_t kSlotInit = 1;  // construction (elem, ctx); rectangles need no ctx (0x8fce0 is ret)
inline constexpr std::size_t kSlotHitTest = 7;         // hit test via children (0x1dfe60)
inline constexpr std::size_t kSlotAttach = 29;         // attach (elem, parent); before that +0x20 = parent set
inline constexpr std::size_t kSlotButtonRelease = 18;  // button: calls callback(input, user)


inline constexpr std::uint32_t kAnchorTopLeft = 0;
inline constexpr std::uint32_t kAnchorTopCenter = 1;
inline constexpr std::uint32_t kAnchorCenter = 4;
inline constexpr std::uint32_t kAnchorBottomLeft = 6;
inline constexpr std::uint32_t kAnchorBottomCenter = 7;


using ElementFn = void (*)(void* self, void* arg);


using RectCtorFn = void* (*)(void* self, float x, float y, float w, float h,
                             std::uint32_t anchor, std::uint32_t color);


using TextCtorFn = void* (*)(void* self, float x, float y, float w, float h,
                             std::uint32_t anchor, MmString* text,
                             std::uint32_t a8, std::uint32_t color,
                             float a10, float a11, std::uint32_t a12);


using TextSetFn = void (*)(std::uintptr_t text, std::uintptr_t ctx, MmString* content);


using AddChildFn = void* (*)(void* child_list, void* child);


using ButtonFactoryFn = void* (*)(void* ctx, float x, float y, std::uint32_t a4,
                                  MmString* icon, MmString* text,
                                  std::uint32_t a7, std::uint32_t action_id,
                                  float a9, float a10);


using ButtonCallbackFn = void (*)(void* input, void* user);

inline constexpr std::uint32_t kRvaMenuViewBuild = 0x6ff5b0;  // c_ui_main_menu_default_view, slot 1
inline constexpr std::uint32_t kRvaMenuRootBuild = 0x704db0;  // c_ui_main_menu_interface,     slot 1
inline constexpr std::uint32_t kRvaMenuViewTick = 0x700310;   // c_ui_main_menu_default_view, slot 3
inline constexpr std::size_t kViewButtonFields[] = {0x80, 0x70, 0x78, 0x98, 0x88, 0x90};

inline constexpr std::uint32_t kRvaLoadVtable = 0xafe930;  // vtable of the loading screen
inline constexpr std::uint32_t kRvaLoadInit = 0x62cf10;    // slot 1 (construction, rdx = ctx)
inline constexpr std::uint32_t kRvaLoadText = 0x62d080;    // (screen, ctx, mm_string*) - set the text
inline constexpr std::uint32_t kRvaLoadTick = 0x251370;    // slot 3 (every frame)

inline constexpr std::size_t kOffLoadCtxOffset = 0xbd750;

inline constexpr std::uint32_t kRvaWorldLoad = 0x741cf0;


inline constexpr std::uint32_t kRvaSplashVtable = 0xb14230;
inline constexpr std::uint32_t kRvaSplashUpdate = 0x27dfd0;
inline constexpr std::uint32_t kRvaSplashDraw = 0x27e330;
inline constexpr std::uint32_t kRvaSplashBuild = 0x27d600;
inline constexpr std::size_t kOffSplashRoot = 0x300;
inline constexpr std::size_t kOffSplashDisplay = 0x324;


inline constexpr std::uint32_t kRvaOceanDepth = 0x2c48f0;
inline constexpr std::uint32_t kRvaDefFind = 0x576680;
inline constexpr std::uint32_t kRvaCellSet = 0x577860;
inline constexpr std::uint32_t kRvaArea = 0x8b9420;
inline constexpr std::uint32_t kRvaWaveRaster = 0x57cfc0;

inline constexpr std::size_t kOffDefList = 0x28;     // list of the tile definitions
inline constexpr std::size_t kOffLevels = 0x40;       // pointer to the levels array
inline constexpr std::size_t kOffLevelsCap = 0x48;    // capacity
inline constexpr std::size_t kOffLevelsStart = 0x4c;  // index of the first slot

inline constexpr std::size_t kLevelSize = 0x270;
inline constexpr std::size_t kOffOriginX = 0x30;  // int
inline constexpr std::size_t kOffOriginY = 0x34;  // int

inline constexpr std::size_t kOffIslandList = 0x88;
inline constexpr std::size_t kIslandEntry = 0x14;


inline constexpr std::size_t kOffDefIsIsland = 0x278;

inline constexpr std::size_t kCellSize = 0xe8;
inline constexpr std::size_t kOffCellIsIsland = 0xbc;
inline constexpr std::size_t kOffCellName = 0xd8;

inline constexpr std::size_t kRingCap = 0x8;
inline constexpr std::size_t kRingStart = 0xc;
inline constexpr std::size_t kRingCount = 0x10;

// AreaFn takes -128..127 per axis:
inline constexpr int kAreaAxis = 128;

inline constexpr std::uint32_t kRvaPlacement = 0x57da40;  // argument capture (args 0..12)
inline constexpr std::uint32_t kRvaAfterwards = 0x559330;  // shortly after the placement

using OceanDepthFn = float (*)(double x, double z, std::uintptr_t island_list);

using DefFindFn = std::uintptr_t (*)(std::uintptr_t list, MmString* name);

using CellSetFn = std::uintptr_t (*)(std::uintptr_t level, std::uint64_t a2, std::uint64_t a3,
                                    std::uint64_t a5, std::uint64_t a6, std::uint64_t a7,
                                    std::uint64_t unused7, std::uint64_t a8,
                                    std::uint64_t a9, std::uint64_t a10, std::uint64_t a11,
                                    int gx, int gy, MmString* name, std::uintptr_t def,
                                    std::uint64_t a13);

using AreaFn = void (*)(std::uintptr_t level, AreaResult* result, const int* min, const int* max);

using WaveRasterFn = void (*)(std::uintptr_t tile_system, std::uint64_t a3);

inline constexpr std::uint32_t kRvaGameSettingsGetter = 0x296940;
using GameSettingsGetFn = std::uintptr_t (*)(void);



inline RectCtorFn Rect = nullptr;
inline TextCtorFn Text = nullptr;
inline OperatorNewFn OperatorNew = nullptr;
inline StringReserveFn StringReserve = nullptr;
inline MemAllocAlignedFn MemAllocAligned = nullptr;
inline StringAssignFn StringAssign = nullptr;
inline StringBufFreeFn StringBufFree = nullptr;
inline StringClearFn StringClear = nullptr;
inline MemSetFn MemSet = nullptr;
inline MemCopyFn MemCopy = nullptr;
inline SinfFn Sinf = nullptr;
inline CosfFn Cosf = nullptr;
inline SqrtDoubleFn SqrtDouble = nullptr;
inline SqrtFloatFn SqrtFloat = nullptr;
inline ArenaAllocFn ArenaAlloc = nullptr;
inline MemCmpFn MemCmp = nullptr;
inline MathSinFn MathSin = nullptr;
inline MathCosFn MathCos = nullptr;
inline MathFloorFn MathFloor = nullptr;
inline MathFmodFn MathFmod = nullptr;
inline StrNCmpFn StrNCmp = nullptr;
inline CrtRandFn CrtRand = nullptr;
inline Mat3MulFn Mat3Mul = nullptr;
inline Mat3IdentityFn Mat3Identity = nullptr;
inline Mat3FromIntsFn Mat3FromInts = nullptr;
inline MmStringConcatFn MmStringConcat = nullptr;
inline CrtErrnoGetFn CrtErrnoGet = nullptr;
inline CrtHeapFreeFn CrtHeapFree = nullptr;
inline Vec3NormalizeFn Vec3Normalize = nullptr;
inline Mat4FromDoublesFn Mat4FromDoubles = nullptr;
inline Mat4InvertFn Mat4Invert = nullptr;
inline Mat4IdentityFn Mat4Identity = nullptr;
inline UiElementAttachFn UiElementAttach = nullptr;
inline UiTextSetFn UiTextSet = nullptr;
inline LuaValueToIntFn LuaValueToInt = nullptr;
inline LuaValueToFloatFn LuaValueToFloat = nullptr;
inline LuaNumberToStringFn LuaNumberToString = nullptr;
inline AddChildFn AddChild = nullptr;
inline TextSetFn TextSet = nullptr;
inline FreeFn Free = nullptr;
inline ButtonFactoryFn ButtonFactory = nullptr;
inline OceanDepthFn OceanDepth = nullptr;
inline DefFindFn DefFind = nullptr;
inline CellSetFn CellSet = nullptr;
inline AreaFn Area = nullptr;
inline WaveRasterFn WaveRaster = nullptr;
inline GameSettingsGetFn GameSettings = nullptr;

inline bool bind(sw::Runtime& rt) {
    const void* pRect = rt.at_rva(kRvaRectCtor);
    const void* pText = rt.at_rva(kRvaTextCtor);
    const void* pNew = rt.at_rva(kRvaOperatorNew);
    const void* pReserve = rt.at_rva(kRvaStringReserve);
    const void* pAllocAligned = rt.at_rva(kRvaMemAllocAligned);
    const void* pAssign = rt.at_rva(kRvaStringAssign);
    const void* pBufFree = rt.at_rva(kRvaStringBufFree);
    const void* pClear = rt.at_rva(kRvaStringClear);
    const void* pMemSet = rt.at_rva(kRvaMemset);
    const void* pMemCopy = rt.at_rva(kRvaMemcpy);
    const void* pSinf = rt.at_rva(kRvaSinf);
    const void* pCosf = rt.at_rva(kRvaCosf);
    const void* pSqrtDouble = rt.at_rva(kRvaSqrtDouble);
    const void* pSqrtFloat = rt.at_rva(kRvaSqrtFloat);
    const void* pArena = rt.at_rva(kRvaArenaAlloc);
    const void* pMemCmp = rt.at_rva(kRvaMemCmp);
    const void* pMathSin = rt.at_rva(kRvaMathSin);
    const void* pMathCos = rt.at_rva(kRvaMathCos);
    const void* pMathFloor = rt.at_rva(kRvaMathFloor);
    const void* pMathFmod = rt.at_rva(kRvaMathFmod);
    const void* pStrNCmp = rt.at_rva(kRvaStrNCmp);
    const void* pCrtRand = rt.at_rva(kRvaCrtRand);
    const void* pErrno = rt.at_rva(kRvaCrtErrnoGet);
    const void* pHeapFree = rt.at_rva(kRvaCrtHeapFree);
    const void* pMat3Mul = rt.at_rva(kRvaMat3Mul);
    const void* pMat3Id = rt.at_rva(kRvaMat3Identity);
    const void* pMat3Int = rt.at_rva(kRvaMat3FromInts);
    const void* pConcat = rt.at_rva(kRvaMmStringConcat);
    const void* pVec3Norm = rt.at_rva(kRvaVec3Normalize);
    const void* pMat4D = rt.at_rva(kRvaMat4FromDoubles);
    const void* pMat4Inv = rt.at_rva(kRvaMat4Invert);
    const void* pMat4Id = rt.at_rva(kRvaMat4Identity);
    const void* pUiAttach = rt.at_rva(kRvaUiElementAttach);
    const void* pUiText = rt.at_rva(kRvaUiTextSet);
    const void* pLuaInt = rt.at_rva(kRvaLuaValueToInt);
    const void* pLuaFloat = rt.at_rva(kRvaLuaValueToFloat);
    const void* pLuaNumStr = rt.at_rva(kRvaLuaNumberToString);
    const void* pAddChild = rt.at_rva(kRvaAddChild);
    const void* pTextSet = rt.at_rva(kRvaTextSet);
    const void* pFree = rt.at_rva(kRvaFree);
    const void* pFactory = rt.at_rva(kRvaButtonFactory);
    const void* pDepth = rt.at_rva(kRvaOceanDepth);
    const void* pSearch = rt.at_rva(kRvaDefFind);
    const void* pCell = rt.at_rva(kRvaCellSet);
    const void* pArea = rt.at_rva(kRvaArea);
    const void* pWaves = rt.at_rva(kRvaWaveRaster);
    const void* pGameSettings = rt.at_rva(kRvaGameSettingsGetter);
    if (!pRect || !pText || !pNew || !pReserve || !pAddChild || !pTextSet ||
        !pFree || !pFactory || !pDepth || !pSearch || !pCell || !pArea ||
        !pWaves || !pAllocAligned || !pAssign || !pBufFree || !pClear ||
        !pMemSet || !pMemCopy || !pSinf || !pCosf || !pSqrtDouble ||
        !pSqrtFloat || !pArena || !pGameSettings || !pMemCmp || !pMathSin ||
        !pMathCos || !pMathFloor || !pCrtRand || !pErrno || !pHeapFree ||
        !pMathFmod || !pStrNCmp ||
        !pMat3Mul || !pMat3Id || !pMat3Int || !pConcat || !pVec3Norm ||
        !pMat4D || !pMat4Inv || !pMat4Id || !pUiAttach || !pUiText ||
        !pLuaInt || !pLuaFloat || !pLuaNumStr)
        return false;
    Rect = reinterpret_cast<RectCtorFn>(pRect);
    Text = reinterpret_cast<TextCtorFn>(pText);
    OperatorNew = reinterpret_cast<OperatorNewFn>(pNew);
    StringReserve = reinterpret_cast<StringReserveFn>(pReserve);
    MemAllocAligned = reinterpret_cast<MemAllocAlignedFn>(pAllocAligned);
    StringAssign = reinterpret_cast<StringAssignFn>(pAssign);
    StringBufFree = reinterpret_cast<StringBufFreeFn>(pBufFree);
    StringClear = reinterpret_cast<StringClearFn>(pClear);
    MemSet = reinterpret_cast<MemSetFn>(pMemSet);
    MemCopy = reinterpret_cast<MemCopyFn>(pMemCopy);
    Sinf = reinterpret_cast<SinfFn>(pSinf);
    Cosf = reinterpret_cast<CosfFn>(pCosf);
    SqrtDouble = reinterpret_cast<SqrtDoubleFn>(pSqrtDouble);
    SqrtFloat = reinterpret_cast<SqrtFloatFn>(pSqrtFloat);
    ArenaAlloc = reinterpret_cast<ArenaAllocFn>(pArena);
    MemCmp = reinterpret_cast<MemCmpFn>(pMemCmp);
    MathSin = reinterpret_cast<MathSinFn>(pMathSin);
    MathCos = reinterpret_cast<MathCosFn>(pMathCos);
    MathFloor = reinterpret_cast<MathFloorFn>(pMathFloor);
    MathFmod = reinterpret_cast<MathFmodFn>(pMathFmod);
    StrNCmp = reinterpret_cast<StrNCmpFn>(pStrNCmp);
    CrtRand = reinterpret_cast<CrtRandFn>(pCrtRand);
    CrtErrnoGet = reinterpret_cast<CrtErrnoGetFn>(pErrno);
    CrtHeapFree = reinterpret_cast<CrtHeapFreeFn>(pHeapFree);
    Mat3Mul = reinterpret_cast<Mat3MulFn>(pMat3Mul);
    Mat3Identity = reinterpret_cast<Mat3IdentityFn>(pMat3Id);
    Mat3FromInts = reinterpret_cast<Mat3FromIntsFn>(pMat3Int);
    MmStringConcat = reinterpret_cast<MmStringConcatFn>(pConcat);
    Vec3Normalize = reinterpret_cast<Vec3NormalizeFn>(pVec3Norm);
    Mat4FromDoubles = reinterpret_cast<Mat4FromDoublesFn>(pMat4D);
    Mat4Invert = reinterpret_cast<Mat4InvertFn>(pMat4Inv);
    Mat4Identity = reinterpret_cast<Mat4IdentityFn>(pMat4Id);
    UiElementAttach = reinterpret_cast<UiElementAttachFn>(pUiAttach);
    UiTextSet = reinterpret_cast<UiTextSetFn>(pUiText);
    LuaValueToInt = reinterpret_cast<LuaValueToIntFn>(pLuaInt);
    LuaValueToFloat = reinterpret_cast<LuaValueToFloatFn>(pLuaFloat);
    LuaNumberToString = reinterpret_cast<LuaNumberToStringFn>(pLuaNumStr);
    AddChild = reinterpret_cast<AddChildFn>(pAddChild);
    TextSet = reinterpret_cast<TextSetFn>(pTextSet);
    Free = reinterpret_cast<FreeFn>(pFree);
    ButtonFactory = reinterpret_cast<ButtonFactoryFn>(pFactory);
    OceanDepth = reinterpret_cast<OceanDepthFn>(pDepth);
    DefFind = reinterpret_cast<DefFindFn>(pSearch);
    CellSet = reinterpret_cast<CellSetFn>(pCell);
    Area = reinterpret_cast<AreaFn>(pArea);
    WaveRaster = reinterpret_cast<WaveRasterFn>(pWaves);
    GameSettings = reinterpret_cast<GameSettingsGetFn>(pGameSettings);
    return true;
}

inline const sw::sym::ResolvedSymbol* resolved_symbols(std::size_t* count = nullptr) {
    if (count) *count = sw::sym::kResolvedSymbolCount;
    return sw::sym::kResolvedSymbols;
}


inline const sw::sym::ResolvedSymbol* resolved_symbol(const char* name) {
    if (name == nullptr) return nullptr;
    const auto* alle = sw::sym::kResolvedSymbols;
    const std::size_t anzahl = sw::sym::kResolvedSymbolCount;
    for (std::size_t i = 0; i < anzahl; ++i) {
        const char* kandidat = alle[i].name;
        std::size_t k = 0;
        while (kandidat[k] != '\0' && name[k] != '\0' && kandidat[k] == name[k]) ++k;
        if (kandidat[k] == '\0' && name[k] == '\0') return &alle[i];
    }
    return nullptr;
}


inline const char* resolved_name(std::uint32_t rva) {
    const auto* alle = sw::sym::kResolvedSymbols;
    std::size_t lo = 0;
    std::size_t hi = sw::sym::kResolvedSymbolCount;
    while (lo < hi) {
        const std::size_t mitte = lo + (hi - lo) / 2;
        if (alle[mitte].rva < rva) {
            lo = mitte + 1;
        } else {
            hi = mitte;
        }
    }
    if (lo < sw::sym::kResolvedSymbolCount && alle[lo].rva == rva) return alle[lo].name;
    return nullptr;
}

}  // namespace sw::protos
#endif  // _WIN64
