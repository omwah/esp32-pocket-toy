{
  // Pomni, from The Amazing Digital Circus: a white disc in a heavy black
  // outline, three straight lashes off the rim, and a six-wedge red and blue
  // pinwheel for an iris. tools/make_pomni_eye.py draws all of it.
  "eyeRadius": 125,
  // The iris is only the pinwheel. Her face, the lashes, the outline and the
  // white of the eye are all in the SCLERA texture, because the eyeball's
  // silhouette stays put on screen while the iris slides about inside it:
  // paint the outline into the iris and it wanders off her face with the
  // gaze. tools/make_pomni_eye.py says more.
  "irisRadius": 42,
  "displaySize": 0,
  "coverage": 0.6,

  // Round pupil, and a wide range: dilating rescales the iris texture
  // radially, which is what makes the pinwheel grow and shrink inside the
  // white the way it does on the show.
  "slitPupilRadius": 0,
  // The pinwheel IS the pupil, so the pupil is drawn from the iris texture
  // rather than as a flat disc: without this a black blot opens in the middle
  // of the wedges as it dilates.
  "texturedPupil": true,
  "pupilMin": 0.10,
  "pupilMax": 0.42,

  // The panel background and the lids are her face, so a blink reads as skin
  // coming down over the eye rather than as a hole in the screen.
  "eyelidColor": [ 246, 243, 239 ],
  "backColor": [ 246, 243, 239 ],
  // Where the wedges meet. Small at rest, and never more than a dot.
  "pupilColor": [ 24, 18, 26 ],

  // The lids hold the OPENING, a circle, so the eye shows as a full disc.
  // That only works with tracking off -- tracking slides the lids with the
  // gaze and would drag them across the middle of the eye.
  "tracking": false,

  "extensions": {
    "display": {
      // Pomni wears her eyes close together. The backend's own layout leaves
      // 28 px between the two squares; this overlaps them by six. The limit is
      // not the drawing at rest but the drawing at full gaze: the sclera is
      // painted on the eyeball and shifts a few pixels as the eye looks
      // about, and past this the left eye's outline crosses into the right
      // eye's square, which then writes its own background over it and leaves
      // a straight vertical cut. Overlapping by twelve did exactly that in 46
      // frames out of 250.
      "eyeGap": -6
    },
    "animation": {
      "autoGaze": true,
      "autoBlink": true,
      // Less than the geometry allows. At full travel the pinwheel slides out
      // of its own white, the drawing reaches the edge of its own square, and
      // where the squares overlap one eye writes its background over the
      // other's outline. Measured over 600 frames, this leaves 5 px between
      // the drawing and its square and never touches the seam.
      "gazeRange": 0.30
    }
  },

  // Eye 0's iris is turned half a turn by the renderer, so that a pair looks
  // like a pair. The lashes live in the sclera now, so the sclera needs the
  // same treatment to keep them with their own eye: up on one, down on the
  // other.
  "scleraAngle": 0,
  "left": { "scleraAngle": 512 },
  "right": { "scleraAngle": 0 },

  "irisTexture": "iris.bmp",
  "scleraTexture": "sclera.bmp",
  "upperEyelid": "upper.bmp",
  "lowerEyelid": "lower.bmp"
}
