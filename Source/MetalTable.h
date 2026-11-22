/*
  ==============================================================================
    Superalloy — MetalTable.h
    The metal menu. Every resistivity is a published value (room temperature,
    ~20–22 °C). The metal enters the model ONLY through rho (Ω·m).

    Source key (full references in README.md):
      [HO]  rev 1–5 HANDOFF §3 (datasheet-sourced in the earlier chats)
      [HER] Heraeus Precious Metals property table (annealed, 20 °C)
      [ZAP] Zapp MP35N precision-wire datasheet (annealed)
      [FWM] Fort Wayne Metals HAYNES 188 datasheet (22 °C)
      [SM6] Special Metals INCONEL 625 bulletin (via Aerospace Metals)
  ==============================================================================
*/
#pragma once

#include <array>

namespace superalloy
{
    struct Metal
    {
        const char* menuName;   // exactly as shown in the "Metal" dropdown
        double rho;             // Ω·m; 0 = superconducting reference
        const char* source;     // provenance tag
        const char* note;       // honest remark shown only in docs/harness
    };

    inline constexpr int kNumMetals = 15;

    inline constexpr std::array<Metal, kNumMetals> kMetals {{
        { "Superconductor (NbTi, 0 \xCE\xA9 reference)", 0.0,      "physics",
          "Zero-resistance winding. The 630 V environment with no metal in it: in-band identical to bypass." },
        { "Silver \xE2\x80\x94 Ohno Continuous Cast 7N",  15.87e-9, "[HO]",
          "OCC crystal structure is not modelled (no physical mechanism); purity enters via rho only." },
        { "Copper \xE2\x80\x94 Grade 1 6N OFC",           16.78e-9, "[HO]",  "Reference metal (100% IACS)." },
        { "Gold \xE2\x80\x94 pure",                        22.14e-9, "[HO]",  "" },
        { "Rhodium",                                       43.0e-9,  "[HER]", "NBS (1929) gives 49.3 nΩ·m; Heraeus annealed value used." },
        { "Iridium",                                       49.0e-9,  "[HER]", "" },
        { "Platinum-Iridium 10 (kilogram-prototype alloy)",250.0e-9, "[HER]", "Pt-10Ir, the alloy of the International Prototype Kilogram." },
        { "Monel 400",                                     0.53e-6,  "[HO]",  "Modelled non-magnetic (owner decision)." },
        { "Haynes 188",                                    1.01e-6,  "[FWM]", "Cobalt-base superalloy." },
        { "MP35N",                                         1.03e-6,  "[ZAP]", "One vendor lists 0.80 µΩ·m; Zapp annealed wire value used." },
        { "Waspaloy",                                      1.22e-6,  "[HO]",  "Fully aged condition." },
        { "Inconel 718",                                   1.24e-6,  "[HO]",  "" },
        { "Hastelloy C-276",                               1.26e-6,  "[HO]",  "" },
        { "Inconel 625",                                   1.29e-6,  "[SM6]", "Electrically identical to Ren\xC3\xA9 41 within published data." },
        { "Ren\xC3\xA9 41",                                1.29e-6,  "[HO]",  "Midpoint of 1.27 (Haynes) – 1.31 (UPM) µΩ·m." },
    }};

    inline constexpr int kDefaultMetal = 2; // Copper (reference)
}
