namespace ovp
{

struct GenSig
{
    int      sym;
    uint32_t len;
    uint32_t rva;
    uint8_t  bytes[32];
    uint8_t  mask[32];
};

// 更新后对应类（MonoSkillButton / 技能系统）在 dump 时刻未加载，
// 旧 8 字节 prologue 在新版代码中命中 50+ 处，无法唯一确定签名。
// 置空以避免按错误签名扫描到无关地址导致误调用。
static const GenSig kGenSigs[] =
{
};

constexpr int kSigCount = 0;

} // namespace ovp
