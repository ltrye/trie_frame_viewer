"""Summarize bounded TFV_TRACE_PATH events without GPU readbacks or media access."""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path


def stats(values):
    if not values:
        return {"samples": 0, "p50Ms": None, "p95Ms": None, "maxMs": None}
    values = sorted(value / 1000 for value in values)
    return {"samples": len(values), "p50Ms": values[int((len(values) - 1) * .5)],
            "p95Ms": values[int((len(values) - 1) * .95)], "maxMs": values[-1]}


def summarize(events):
    groups = defaultdict(dict)
    counts = Counter(event["stage"] for event in events)
    for event in events:
        if event["request"]:
            groups[event["request"]].setdefault(event["stage"], event["tUs"])
    def duration(start, end):
        return stats([stages[end] - stages[start] for stages in groups.values()
                      if start in stages and end in stages and stages[end] >= stages[start]])
    audio = defaultdict(list)
    pending = {}
    for event in events:
        stage = event["stage"]
        for operation in ("audio.setup", "audio.stop"):
            if stage == operation + ".begin":
                pending[operation] = event["tUs"]
            elif stage == operation + ".end" and operation in pending:
                audio[operation].append(event["tUs"] - pending.pop(operation))
    presented = [event["tUs"] for event in events if event["stage"] == "present.queued"]
    return {
        "counts": dict(counts),
        "inputToGui": duration("input", "gui.accepted"),
        "inputToPresentationQueue": duration("input", "present.queued"),
        "workerQueueWait": duration("request", "worker.begin"),
        "foregroundDecode": duration("decode.begin", "decode.end"),
        "audioSetup": stats(audio["audio.setup"]), "audioStop": stats(audio["audio.stop"]),
        "maxPresentationGapMs": max((b-a for a, b in zip(presented, presented[1:])), default=0) / 1000,
        "pixelBytesConstructed": sum(event["value"] for event in events if event["stage"] == "pixels.end"),
        "uploadBytesEnqueued": sum(event["value"] for event in events if event["stage"] == "upload.enqueued"),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    trace = json.loads(args.trace.read_text(encoding="utf-8"))
    if trace.get("schema") != 1:
        raise SystemExit("Unsupported trace schema")
    events = sorted(trace["events"], key=lambda event: event["tUs"])
    windows = []
    begin = None
    for event in events:
        if event["stage"] == "measure.begin":
            begin = event
        elif event["stage"] == "measure.end" and begin:
            selected = [item for item in events if begin["tUs"] <= item["tUs"] <= event["tUs"]]
            windows.append({"audioEnabled": bool(begin["value"]), **summarize(selected)})
            begin = None
    result = {"trace": str(args.trace), "complete": trace["droppedEvents"] == 0,
              "droppedEvents": trace["droppedEvents"], "events": len(events),
              "presentationMarker": "queued for presentation, not physical monitor display",
              "overall": summarize(events), "measurementWindows": windows}
    output = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(output, encoding="utf-8")
    print(output, end="")


if __name__ == "__main__":
    main()
