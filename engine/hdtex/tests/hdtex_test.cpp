

#include "hd_textures.h"

#include <cstdio>
#include <fstream>

namespace fs = std::filesystem;
namespace h = engine::hdtex;

static int sFailed = 0;
#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #cond);     \
      sFailed++;                                                          \
    }                                                                     \
  } while (0)

static void Touch(const fs::path& p) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << "x";
}

int main(int argc, char** argv) {

  h::Name n;
  n.width = 64;
  n.height = 32;
  n.texHash = 0x0123456789abcdefull;
  n.format = 5;
  CHECK(h::FormatName(n) == "tex1_64x32_0123456789abcdef_5");
  n.mipmapped = true;
  n.hasTlut = true;
  n.tlutHash = 0xabull;
  n.format = 9;
  CHECK(h::FormatName(n) == "tex1_64x32_m_0123456789abcdef_00000000000000ab_9");
  n.wildcardTlut = true;
  CHECK(h::FormatName(n) == "tex1_64x32_m_0123456789abcdef_$_9");
  n.wildcardTex = true;
  n.wildcardTlut = false;
  CHECK(h::FormatName(n) == "tex1_64x32_m_$_00000000000000ab_9");


  auto p = h::ParseName("tex1_64x32_m_0123456789abcdef_00000000000000ab_9");
  CHECK(p && p->width == 64 && p->height == 32 && p->mipmapped && p->hasTlut && p->format == 9 &&
        p->texHash == 0x0123456789abcdefull && p->tlutHash == 0xab && p->mipLevel == 0 && !p->arbitraryMipmaps);
  p = h::ParseName("tex1_8x8_0123456789abcdef_14_arb");
  CHECK(p && p->arbitraryMipmaps && !p->hasTlut && p->format == 14 && !p->mipmapped);
  p = h::ParseName("tex1_8x8_0123456789abcdef_14_mip3");
  CHECK(p && p->mipLevel == 3);
  p = h::ParseName("tex1_8x8_$_14");
  CHECK(p && p->wildcardTex);
  p = h::ParseName("tex1_8x8_0123456789abcdef_$_9");
  CHECK(p && p->wildcardTlut && p->hasTlut);
  CHECK(!h::ParseName("readme"));
  CHECK(!h::ParseName("tex1_8x8_zz_14"));
  CHECK(!h::ParseName("tex1_8x8_14"));
  CHECK(!h::ParseName("tex1_0x8_0123456789abcdef_14"));
  CHECK(!h::ParseName("tex2_8x8_0123456789abcdef_14"));

  const std::string s = "tex1_256x128_m_fedcba9876543210_0000000000000001_10";
  CHECK(h::FormatName(*h::ParseName(s)) == s);


  const fs::path root = fs::temp_directory_path() / "hdtex_test_root";
  fs::remove_all(root);
  Touch(root / "GXXE01" / "tex1_8x8_0123456789abcdef_5.png");
  Touch(root / "GXXE01" / "sub" / "tex1_8x8_0123456789abcdee_5_arb.dds");
  Touch(root / "GXXE01" / "sub" / "tex1_8x8_0123456789abcdee_5_mip1.dds");
  Touch(root / "GXXE01" / "sub" / "tex1_8x8_$_0000000000000002_9.png");
  Touch(root / "GXXE01" / "notes.txt");
  Touch(root / "MyPack" / "GXXE01.txt");
  Touch(root / "MyPack" / "tex1_16x16_0123456789abcdef_5.PNG");
  Touch(root / "Other" / "GALE01.txt");
  Touch(root / "Other" / "tex1_16x16_0123456789abcdef_5.png");
  const auto folders = h::FindPackFolders(root, "GXXE01");
  CHECK(folders.size() == 2);
  if (folders.size() == 2) {
    CHECK(folders[0] == root / "GXXE01");
    CHECK(folders[1] == root / "MyPack");
    const h::PackInfo a = h::ScanPack(folders[0]);
    CHECK(a.textures == 3 && a.mipFiles == 1 && a.arb == 1 && a.wildcards == 1 && a.otherFiles == 1);
    const h::PackInfo b = h::ScanPack(folders[1]);
    CHECK(b.textures == 1 && b.otherFiles == 1);
  }
  fs::remove_all(root);
  fs::create_directories(root / "GXX");
  CHECK(h::FindPackFolders(root, "GXXE01").size() == 1);
  fs::remove_all(root);
  CHECK(h::FindPackFolders(root, "GXXE01").empty());


  uint8_t px[4 * 3 * 2];
  for (int i = 0; i < 24; i++) px[i] = static_cast<uint8_t>(i * 10);
  const auto png = h::EncodePngRgba(3, 2, px);
  CHECK(png.size() > 60 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G');
  if (argc > 1) {
    CHECK(h::WritePngRgba(fs::path(argv[1]) / "test.png", 3, 2, px));

    std::vector<uint8_t> big(300 * 200 * 4);
    for (size_t i = 0; i < big.size(); i++) big[i] = static_cast<uint8_t>(i * 7 + i / 300);
    CHECK(h::WritePngRgba(fs::path(argv[1]) / "big.png", 300, 200, big.data()));
  }
  std::printf(sFailed == 0 ? "hdtex_test: ok\n" : "hdtex_test: %d FAILED\n", sFailed);
  return sFailed == 0 ? 0 : 1;
}
