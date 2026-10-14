# .tfv project format, version 1

UTF-8 JSON, one source per project. Media pixels/audio are external and the cache is
not saved. Internal frame indices are zero-based; the interface displays index + 1.
Range endpoints are inclusive. Pan is measured in Qt logical pixels relative to
the viewer center; zoom is a multiplier of fit-to-window.

```json
{
  "application": "Trie FrameViewer",
  "schemaVersion": 1,
  "media": {
    "path": "../playblasts/walk.mp4",
    "identity": {"size": "12345", "sampleSha256": "..."}
  },
  "frame": 24,
  "range": {"start": 4, "endInclusive": 36},
  "playback": {"speed": 1, "loop": "loop", "audioOffsetMs": 0},
  "view": {
    "zoom": 1, "panX": 0, "panY": 0, "rotation": 0,
    "flipHorizontal": false, "flipVertical": false
  },
  "bookmarks": [
    {"id": "unique-id", "type": "frame", "name": "Contact", "color": "#e6bf79", "start": 24, "endInclusive": 24},
    {"id": "another-id", "type": "frame", "name": "Passing", "color": "#a8d5c2", "start": 4, "endInclusive": 4}
  ]
}
```

`loop` accepts `once`, `loop`, or `pingpong`. A positive audio offset delays audio
relative to video. Frame-bookmark endpoints must be equal. Bookmark IDs must be
nonempty and unique and endpoints must be inside the loaded source.

Only single-frame bookmarks are created and saved. The version 1 wire format keeps
equal `start`/`endInclusive` fields for compatibility. Older `type: "range"` bookmarks
are validated and loaded as a bookmark at their start frame, retaining the ID, name,
and color. They save as `type: "frame"` on the next save. Playback range settings are
independent of bookmarks and remain supported.

Projects larger than 4 MiB and unknown schema versions are rejected. Media identity
uses size plus a SHA-256 of the first/last 64 KiB; this detects many replacement
files without hashing a large clip. It can miss modifications in the middle.

Every 30 seconds a modified project with an existing save location is saved to
`<project>.tfv.autosave`. Open that file explicitly to recover it. Autosave files
do not embed footage and are not automatically deleted.

Save prompts and the unsaved indicator require at least one current bookmark.
Deleting the last bookmark clears that state. Changing playback speed never marks
the project modified; the current speed is still written on an explicit save.
Other bookmark/review-setting edits mark the project modified while bookmarks exist.
Manual saving remains available with zero bookmarks, including saving their removal.

For image sequences, the reference points to a selected frame and the current
decoder discovers matching numbered siblings. The source identity currently checks
the selected image only. Sequence-wide fingerprints and stable source numbering
must be added before relying on project identity after sequence edits.
