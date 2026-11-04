"""Generate original test-only artwork; no third-party font data.

Run with fonttools==4.60.1. The checked-in TTF is the test input, so normal
configure/build/test/install needs neither Python nor fontTools. The fixture
and generator use the repository's Apache-2.0 license.
"""

from pathlib import Path

from fontTools.feaLib.builder import addOpenTypeFeaturesFromString
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen


names = [
    ".notdef", "space", "A", "B", "f", "i", "fi", "e", "acutecomb", "eacute",
    "beh", "beh.isol", "beh.init", "beh.medi", "beh.fina",
]
widths = {name: 600 for name in names}
widths.update({".notdef": 500, "space": 250, "f": 400, "i": 200, "fi": 900, "acutecomb": 0})

builder = FontBuilder(1000, isTTF=True)
builder.setupGlyphOrder(names)
builder.setupCharacterMap({
    32: "space", 65: "A", 66: "B", 102: "f", 105: "i", 101: "e",
    0x301: "acutecomb", 0xE9: "eacute", 0x628: "beh",
})
glyphs = {}
for name in names:
    pen = TTGlyphPen(None)
    if name != "space":
        width = max(100, widths[name] - 100)
        pen.moveTo((0, 0))
        pen.lineTo((width, 0))
        pen.lineTo((width, 700))
        pen.lineTo((0, 700))
        pen.closePath()
    glyphs[name] = pen.glyph()
builder.setupGlyf(glyphs)
builder.setupHorizontalMetrics({name: (widths[name], 0) for name in names})
builder.setupHorizontalHeader(ascent=800, descent=-200)
builder.setupOS2(sTypoAscender=800, sTypoDescender=-200, usWinAscent=800, usWinDescent=200)
builder.setupNameTable({
    "familyName": "Truffle Original Unicode Test",
    "styleName": "Regular",
    "uniqueFontIdentifier": "Truffle Original Unicode Test 1",
    "fullName": "Truffle Original Unicode Test",
    "psName": "TruffleOriginalUnicodeTest",
    "version": "Version 1.0",
    "licenseDescription": "Original test artwork, Apache-2.0; see repository LICENSE.",
})
builder.setupPost()
builder.setupMaxp()
addOpenTypeFeaturesFromString(builder.font, """
languagesystem DFLT dflt;
languagesystem latn dflt;
languagesystem arab dflt;
feature liga {
    script latn;
    sub f i by fi;
} liga;
feature isol {
    script arab;
    sub beh by beh.isol;
} isol;
feature init {
    script arab;
    sub beh by beh.init;
} init;
feature medi {
    script arab;
    sub beh by beh.medi;
} medi;
feature fina {
    script arab;
    sub beh by beh.fina;
} fina;
table GDEF {
    LigatureCaretByPos fi 200;
} GDEF;
""")
builder.font["head"].created = 2082844800
builder.font["head"].modified = 2082844800
builder.font.recalcTimestamp = False
builder.save(Path(__file__).with_name("TruffleUnicodeTest.ttf"))
