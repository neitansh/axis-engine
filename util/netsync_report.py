#!/usr/bin/env python3
"""Turns the CSV written by netsync_diagnostics into a verdict.

Shaking is a change of speed between frames, so the numbers that matter are
the distribution of that change - its median, its tail, and how often motion
stops altogether. Run after a session with "netsync_diagnostics_log" on:

    util/netsync_report.py                     # reads netsync/ next to the client
    util/netsync_report.py path/to/netsync     # or a directory given by hand
"""

import csv
import os
import statistics
import sys


def read(path):
    if not os.path.isfile(path):
        return []
    with open(path, newline="", encoding="utf-8") as fh:
        return list(csv.DictReader(fh))


def numbers(rows, field):
    out = []
    for row in rows:
        try:
            out.append(float(row[field]))
        except (KeyError, ValueError, TypeError):
            pass
    return out


def spread(values):
    """Median, 99th percentile and worst, the shape of a jitter distribution."""
    if not values:
        return (0.0, 0.0, 0.0)
    ordered = sorted(values)
    p99 = ordered[min(len(ordered) - 1, int(0.99 * len(ordered)))]
    return (statistics.median(ordered), p99, ordered[-1])


def report_frames(rows):
    if not rows:
        print("No frames recorded.")
        return

    dtimes = numbers(rows, "dtime")
    print("Frames: %d over %.1f s, %.1f fps average" % (
            len(rows), sum(dtimes), len(dtimes) / max(sum(dtimes), 1e-6)))
    print("  frame time  median %.1f ms  p99 %.1f ms  worst %.1f ms" %
            tuple(v * 1000 for v in spread(dtimes)))

    riding = [r for r in rows if r.get("platform_id", "0") not in ("0", "")]
    print("  frames riding a platform: %d" % len(riding))

    def block(title, source, speed_field, jerk_field):
        if not source:
            return
        speeds = numbers(source, speed_field)
        jerks = numbers(source, jerk_field)
        moving = [j for s, j in zip(speeds, jerks) if s > 0.05]

        print("\n%s" % title)
        print("  speed       median %.3f  p99 %.3f  worst %.3f blocks/s" %
                spread(speeds))
        print("  jerk        median %.3f  p99 %.3f  worst %.3f blocks/s per frame" %
                spread(jerks))
        if moving:
            print("  jerk moving median %.3f  p99 %.3f  worst %.3f" % spread(moving))
        still = sum(1 for s in speeds if s < 0.001)
        print("  frames with no motion at all: %d of %d (%.1f%%)" %
                (still, len(speeds), 100.0 * still / max(len(speeds), 1)))

    block("Object being watched", [r for r in rows if r.get("object_id") not in ("0", "", None)],
            "object_speed", "object_jerk")
    block("Local player", rows, "player_speed", "player_jerk")

    behind = numbers(rows, "buffer_behind")
    target = numbers(rows, "buffer_target")
    quiet = numbers(rows, "frames_since_packet")
    if behind:
        dry = sum(1 for b, t in zip(behind, target) if t > 0 and b < 0.001)
        print("\nPlayback buffer")
        print("  depth       median %.1f ms  p99 %.1f ms  worst %.1f ms" %
                tuple(v * 1000 for v in spread(behind)))
        print("  target      median %.1f ms" % (spread(target)[0] * 1000))
        print("  frames with an empty buffer: %d (%.1f%%)" %
                (dry, 100.0 * dry / max(len(behind), 1)))
        print("  frames since a packet: median %.0f  p99 %.0f  worst %.0f" %
                spread(quiet))


def report_packets(rows):
    if not rows:
        return

    by_object = {}
    for row in rows:
        by_object.setdefault(row["object_id"], []).append(row)

    print("\nPackets")
    for object_id, packets in sorted(by_object.items(), key=lambda kv: -len(kv[1])):
        gaps = [g for g in numbers(packets, "gap_since_previous") if g > 0]
        intervals = numbers(packets, "interval")
        if not gaps:
            continue
        name = packets[0].get("object_name") or "?"
        print("  #%s %s: %d packets" % (object_id, name, len(packets)))
        print("    arrival gap median %.1f ms  p99 %.1f ms  worst %.1f ms" %
                tuple(v * 1000 for v in spread(gaps)))
        print("    server said     median %.1f ms  p99 %.1f ms  worst %.1f ms" %
                tuple(v * 1000 for v in spread(intervals)))
        if len(gaps) > 2:
            print("    gap spread (stdev) %.1f ms" % (statistics.pstdev(gaps) * 1000))


def main():
    directory = sys.argv[1] if len(sys.argv) > 1 else "netsync"

    frames = read(os.path.join(directory, "frames.csv"))
    packets = read(os.path.join(directory, "packets.csv"))

    if not frames and not packets:
        sys.exit("Nothing recorded in %s. Is netsync_diagnostics_log on?" % directory)

    report_frames(frames)
    report_packets(packets)


if __name__ == "__main__":
    main()
