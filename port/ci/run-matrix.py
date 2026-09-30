#!/usr/bin/env python3
"""Run the portability matrix on an existing Linux Docker host, without GitHub."""
import argparse
import datetime
import hashlib
import json
import platform
import shutil
import subprocess
import sys
import tarfile
import time
import uuid
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/tools"))
from make_source import create


def load_targets():
    return json.loads((ROOT / "port/ci/targets.json").read_text())


def plan(targets, scope):
    jobs = [{"name": "kernel-" + t["id"], "action": "kernel", "target": t["id"], "needs": []}
            for t in targets]
    if scope == "all":
        jobs.append({"name": "baseline-el8", "action": "baseline", "target": "el8", "needs": []})
        for t in targets:
            desktop = "desktop-" + t["id"]
            jobs.append({"name": desktop, "action": "desktop", "target": t["id"], "needs": ["baseline-el8"]})
            jobs.append({"name": "runtime-" + t["id"], "action": "runtime", "target": t["id"], "needs": [desktop]})
    return jobs


def execute_plan(jobs, execute, progress):
    results = {}
    for job in jobs:
        unmet = [name for name in job["needs"] if results[name]["status"] != "passed"]
        if unmet:
            result = {"status": "blocked", "reason": "prerequisites did not pass: " + ", ".join(unmet)}
        else:
            print("START " + job["name"], flush=True)
            started = time.monotonic()
            try:
                result = execute(job)
            except Exception as error:
                result = {"status": "failed", "reason": type(error).__name__ + ": " + str(error)}
            result["duration_seconds"] = round(time.monotonic() - started, 2)
        results[job["name"]] = dict(job, **result)
        print(result["status"].upper() + " " + job["name"], flush=True)
        progress(results)
    return results


def verdict(results, selected, scope):
    expected = {job["name"] for job in plan(selected, scope)}
    passed = bool(expected) and set(results) == expected and all(
        row["status"] == "passed" for row in results.values())
    complete = scope == "all" and {t["id"] for t in selected} == {t["id"] for t in load_targets()}
    return {"requested_scope_passed": passed, "full_matrix_passed": passed and complete,
            "hardware_acceptance": "not_run"}


def extract_snapshot(archive, destination):
    """Only extract regular files/directories from our source snapshot."""
    destination.mkdir(parents=True)
    base = destination.resolve()
    with tarfile.open(archive) as source:
        members = source.getmembers()
        prefixes = set()
        for member in members:
            name = PurePosixPath(member.name)
            if name.is_absolute() or ".." in name.parts or not name.parts:
                raise ValueError("unsafe archive member: " + member.name)
            prefixes.add(name.parts[0])
            # make_source materializes file links. Refuse links/devices here,
            # including link chains whose lexical paths appear to stay inside.
            if not (member.isfile() or member.isdir()):
                raise ValueError("unsupported archive member: " + member.name)
            if base not in (base / member.name).resolve().parents:
                raise ValueError("archive member escapes snapshot: " + member.name)
        if len(prefixes) != 1:
            raise ValueError("source archive needs a single root directory")
        # Python 3.8 has no tarfile extraction filter; the validation above is
        # required on the oldest host interpreter as well as newer ones.
        source.extractall(destination)
    return destination / prefixes.pop()


class DockerRunner:
    def __init__(self, output, archive, timeout, targets):
        self.output, self.archive, self.timeout = output, archive, timeout
        self.targets = {t["id"]: t for t in targets}
        self.images = {}
        self.sources = {}
        self.run_id = uuid.uuid4().hex[:12]

    def command(self, args, log, timeout=None):
        with log.open("a", encoding="utf-8") as stream:
            stream.write("argv: " + json.dumps([str(a) for a in args]) + "\n")
            stream.flush()
            try:
                process = subprocess.run(args, stdin=subprocess.DEVNULL, stdout=stream,
                                         stderr=subprocess.STDOUT, timeout=timeout or self.timeout)
                code = process.returncode
            except subprocess.TimeoutExpired:
                stream.write("TIMEOUT\n")
                code = 124
        return code

    def image(self, target):
        tag = self.targets[target]["image"]
        if tag not in self.images:
            metadata = self.output / "images"
            metadata.mkdir(exist_ok=True)
            log = metadata / (target + "-pull.log")
            code = self.command(["docker", "pull", "--platform", "linux/amd64", tag], log)
            if code:
                raise RuntimeError("image pull failed (exit {}); see {}".format(code, log))
            raw = subprocess.check_output(["docker", "image", "inspect", tag], timeout=60)
            info = json.loads(raw)[0]
            if info["Os"] != "linux" or info["Architecture"] != "amd64":
                raise ValueError("image must be linux/amd64: " + tag)
            (metadata / (target + ".json")).write_bytes(raw)
            self.images[tag] = info["Id"]
        # Use this run's immutable image ID for both build and fresh runtime.
        return self.images[tag]

    def __call__(self, job):
        work = self.output / "jobs" / job["name"]
        work.mkdir(parents=True)
        log = work / "console.log"
        action, target = job["action"], job["target"]
        if action == "runtime":
            source = self.sources["desktop-" + target]
            artifacts = self.output / "jobs" / ("desktop-" + target) / "artifacts"
        else:
            source = extract_snapshot(self.archive, work / "source")
            self.sources[job["name"]] = source
            artifacts = work / "artifacts"
            artifacts.mkdir()
        row = {"log": str(log.relative_to(self.output)),
               "artifacts": str(artifacts.relative_to(self.output))}
        if action == "baseline":
            code = self.command([sys.executable, str(source / "port/tools/build_gui.py"), "--preflight"], log)
            if code:
                return dict(row, status="failed", exit_code=code, reason="GUI source preflight failed")
        image_id = self.image(target)
        name = "octool-" + self.run_id + "-" + job["name"]
        command = ["docker", "run", "--rm", "--init", "--name", name, "--platform", "linux/amd64",
                   "-e", "OCTOOL_DISPOSABLE_CONTAINER=1", "-v", str(source) + ":/src" + (":ro" if action == "runtime" else ""),
                   "-v", str(artifacts) + ":/out", "-w", "/src"]
        if action == "desktop":
            inputs = self.output / "jobs/baseline-el8/artifacts"
            command += ["-v", str(inputs) + ":/inputs:ro"]
        command += [image_id, "bash", "port/ci/in-container.sh", action, target]
        code = None
        try:
            code = self.command(command, log)
        finally:
            if code is None or code == 124:
                # Killing the Docker client does not stop its daemon's container.
                # Only remove the unique container created by this invocation.
                self.command(["docker", "rm", "-f", name], log, timeout=30)
        return dict(row, status="passed" if code == 0 else "failed", exit_code=code, image_id=image_id)


def save_report(output, report):
    temporary = output / "results.json.tmp"
    temporary.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    temporary.replace(output / "results.json")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scope", choices=("all", "kernel"), default="all")
    parser.add_argument("--targets", nargs="+", help="default: all 10; a subset is never full acceptance")
    parser.add_argument("--plan", action="store_true", help="show the graph without starting Docker jobs")
    parser.add_argument("--output", type=Path, help="new, non-existing run directory")
    parser.add_argument("--timeout-minutes", type=int, default=180, help="timeout per command/container")
    args = parser.parse_args()
    targets = load_targets()
    names = args.targets or [t["id"] for t in targets]
    if len(names) != len(set(names)) or set(names) - {t["id"] for t in targets}:
        parser.error("targets must be unique IDs from port/ci/targets.json")
    if args.timeout_minutes <= 0:
        parser.error("timeout must be positive")
    selected = [next(t for t in targets if t["id"] == name) for name in names]
    jobs = plan(selected, args.scope)
    if args.plan:
        print(json.dumps({"scope": args.scope, "targets": names, "jobs": jobs,
                          "executed": False, "full_matrix_passed": False}, indent=2))
        return 0
    if platform.system() != "Linux" or platform.machine() not in ("x86_64", "amd64"):
        parser.error("execute this runner on the supplied Linux x86_64 build host; --plan also works on Windows")
    if not shutil.which("docker"):
        parser.error("Docker is not installed on this Linux host")
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    output = (args.output or ROOT / "build/cloud-runs" / (stamp + "-" + uuid.uuid4().hex[:6])).resolve()
    if any(c in str(output) for c in (":", "\n", "\r")):
        parser.error("run path cannot contain colon or newline (Docker bind mount syntax)")
    if output == ROOT or ROOT in output.parents and output.relative_to(ROOT).parts[0] not in ("build", "dist"):
        parser.error("in-tree run output must be under build/ or dist/ so it cannot enter the source snapshot")
    # Never overwrite a previous run's source copies, artifacts or evidence.
    output.mkdir(parents=True, exist_ok=False)
    report = {"started_utc": stamp, "scope": args.scope, "targets": names, "jobs": {},
              "host": {"platform": platform.platform(), "python": platform.python_version()},
              "requested_scope_passed": False, "full_matrix_passed": False, "hardware_acceptance": "not_run"}
    save_report(output, report)
    print("Evidence: " + str(output), flush=True)
    try:
        docker = subprocess.check_output(["docker", "info", "--format", "{{.OSType}} {{.Architecture}}"],
                                         stderr=subprocess.STDOUT, timeout=60, text=True).strip()
        if docker not in ("linux x86_64", "linux amd64"):
            raise ValueError("Docker daemon must be Linux x86_64: " + docker)
        report["host"]["docker"] = docker
        # Host tests are static/source checks; each target also compiles and runs
        # C loopback + parity selftest inside its own container.
        with (output / "local-checks.log").open("w", encoding="utf-8") as log:
            for command in ([sys.executable, "port/ci/validate-local.py"],
                            [sys.executable, "-m", "unittest", "discover", "-s", "port/tests", "-p", "test_*.py", "-v"]):
                subprocess.run(command, cwd=ROOT, check=True, stdout=log, stderr=subprocess.STDOUT, timeout=300)
        archive = create(output / "input")
        report["source"] = {"archive": str(archive.relative_to(output)),
                            "sha256": hashlib.sha256(archive.read_bytes()).hexdigest()}
        runner = DockerRunner(output, archive, args.timeout_minutes * 60, targets)

        def progress(results):
            report["jobs"] = results
            report.update(verdict(results, selected, args.scope))
            save_report(output, report)

        results = execute_plan(jobs, runner, progress)
        report.update(verdict(results, selected, args.scope))
    except KeyboardInterrupt:
        report["error"] = "interrupted; incomplete results cannot pass"
        report["requested_scope_passed"] = report["full_matrix_passed"] = False
    except Exception as error:
        report["error"] = type(error).__name__ + ": " + str(error)
        report["requested_scope_passed"] = report["full_matrix_passed"] = False
    report["finished_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    save_report(output, report)
    print(json.dumps({key: report[key] for key in ("requested_scope_passed", "full_matrix_passed", "hardware_acceptance")}))
    return 0 if report["requested_scope_passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
