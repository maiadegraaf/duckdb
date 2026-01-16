import os
import subprocess
import sys
import argparse
import json
import re
from github import Github

import duckdb

FAILURE = False
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class Color:
    PURPLE = '\033[95m'
    CYAN = '\033[96m'
    DARKCYAN = '\033[36m'
    BLUE = '\033[94m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    RED = '\033[91m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'
    END = '\033[0m'


def print_success(msg: str, indent=1):
    space = " " * (indent * 2)
    print(f"{space}{Color.GREEN}✓{Color.END} {msg}")


def print_error(msg: str, indent=1):
    global FAILURE
    FAILURE = True
    space = " " * (indent * 2)
    print(f"{space}{Color.RED}✗{Color.END} {msg}")


def print_step(msg: str):
    print(f"\n→ {msg}")


class Settings:
    def __init__(self, binary, prev_version, current_version, current_hash, platform):
        self.binary = binary
        self.prev_version = prev_version
        self.current_version = current_version
        self.current_hash = current_hash
        self.platform = platform
        self.expected_codename = get_expected_codename(current_version)


class BinaryInfo:
    def __init__(self, binary):
        version_info = run_duckdb_query_json(binary, "PRAGMA version;")[0]
        self.lib_version = version_info['library_version']
        self.source_id = version_info['source_id']
        self.codename = version_info['codename']


def run_command(command, shell=True, env=None, capture_output=False):
    if not capture_output:
        process = subprocess.Popen(
            command, shell=shell, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env
        )
        for line in process.stdout:
            print(line, end='')
        process.wait()
        if process.returncode != 0:
            print(f"Command failed with exit code {process.returncode}")
        return None
    else:
        result = subprocess.run(command, shell=shell, capture_output=True, text=True, env=env)
        if result.returncode != 0:
            print(f"Command failed with exit code {result.returncode}")
            print(f"STDOUT: {result.stdout}")
            print(f"STDERR: {result.stderr}")
        return result.stdout


def run_duckdb_query(binary, query):
    run_command(f"{binary} -c \"{query}\"")


def run_duckdb_query_json(binary, query):
    output = run_command(f"{binary} -json -c \"{query}\"", capture_output=True)
    try:
        return json.loads(output)
    except json.JSONDecodeError as e:
        print(f"Failed to parse JSON output: {e}")
        print(f"Output was: {output}")
        sys.exit(1)


def get_expected_codename(lib_version):
    if "-dev" in lib_version:
        return "Development Version"

    source_path = os.path.join(REPO_ROOT, 'src', 'function', 'table', 'version', 'pragma_version.cpp')

    if not os.path.exists(source_path):
        if "-dev" in lib_version:
            return "Development Version"
        print(f"Error: Could not find {source_path} to parse codename.")
        sys.exit(1)

    try:
        with open(source_path, 'r') as f:
            content = f.read()

        # Look for the ReleaseCodename function
        match = re.search(r'const char \*DuckDB::ReleaseCodename\(\) \{(.*)}', content, re.DOTALL)
        if not match:
            return "Unknown Version"

        body = match.group(1)
        versions = re.findall(r'StringUtil::StartsWith\(DUCKDB_VERSION, "(.*?)"\).*?return "(.*?)";', body, re.DOTALL)
        for prefix, codename in versions:
            if lib_version.startswith(prefix):
                return codename

    except Exception as e:
        print(f"Warning: Error parsing {source_path}: {e}")

    return "Unknown Version"


def evaluate_comparison(binary_value, expected_value, name: str):
    if binary_value == expected_value:
        print_success(f"{name}")
        return

    print_error(f"{name} mismatch! Expected {expected_value}, got {binary_value}")


def evaluate_if_present(needle, haystack, name):
    if (isinstance(needle, bool) and needle == haystack) or needle in haystack:
        print_success(f"{name}: {needle}")
    else:
        print_error(f"{name} not found, Expected {needle}, got {haystack}.")


def check_version(settings: Settings, info: BinaryInfo):
    binary_lib_version = info.lib_version
    binary_source_id = info.source_id
    binary_codename = info.codename

    evaluate_comparison(binary_lib_version, settings.current_version, "Version")

    evaluate_comparison(binary_codename, settings.expected_codename, "Codename")

    if not binary_source_id or len(binary_source_id) < 7:
        print_error(f"Error: Invalid Source ID (hash): {binary_source_id}")

    if settings.current_hash:
        evaluate_comparison(binary_lib_version, settings.current_version[:7], "Hash")


def get_latest_extension_commit():
    g = Github()
    repo = g.get_repo("duckdb/duckdb-httpfs")
    latest_commit = repo.get_commits()[0]
    return latest_commit.sha[:7]


def extension_autoloading(duckdb_binary):
    query = "SET autoload_known_extensions=1; SET s3_region='us-east-1'; SELECT extension_name, extension_version, loaded FROM duckdb_extensions() WHERE extension_name='httpfs'"
    extensions_info = run_duckdb_query_json(duckdb_binary, query)
    if not extensions_info:
        print_error("Error: httpfs extension not found in duckdb_extensions()")
        return

    extensions_info = extensions_info[0]
    evaluate_if_present('httpfs', extensions_info['extension_name'], 'Extension name')

    # latest_extension_commit = get_latest_extension_commit()
    # evaluate_if_present(latest_extension_commit, extensions_info['extension_version'], 'Extension version')

    evaluate_if_present(True, extensions_info['loaded'], 'Extension loaded')


def secret_compatibility(duckdb_binary):
    try:
        duckdb.query('DROP PERSISTENT SECRET IF EXISTS test_secret')
        duckdb.query('CREATE PERSISTENT SECRET test_secret (TYPE s3, KEY_ID \'fake\', SECRET \'also_fake\')')

        result = run_duckdb_query_json(duckdb_binary, "select secret_string from duckdb_secrets();")
        if result and len(result) > 0 and 'key_id=fake' in result[0]['secret_string']:
            print_success(f"Persistent secret successfully written and retrieved")
            return
        print_error(f"Error retrieving or writing secret.")
    except Exception as e:
        print_error(f"Error retrieving or writing secret: {e}")


def database_version_compatability(duckdb_binary, new_version):
    try:
        run_duckdb_query(
            duckdb_binary,
            f"ATTACH 'test_latest.db' as t (STORAGE_VERSION '{new_version}'); CREATE OR REPLACE TABLE t.test AS SELECT 'success!' as a;",
        )
        run_duckdb_query(
            duckdb_binary,
            f"ATTACH 'test_old.db' as t (STORAGE_VERSION 'v1.2.0'); CREATE OR REPLACE TABLE t.test AS SELECT 'success!' as a;",
        )
        print_success(f"Database with old storage version compatible with {new_version}")

        duckdb.query("ATTACH 'test_old.db' as t; USE t; FROM test;")
        print_success(f"Database created with {new_version} compatible with v1.2.0")
    except Exception as e:
        print_error(f"Error creating new database: {e}")


def capi_compatibility(duckdb_binary, new_version, platform):
    run_duckdb_query(
        duckdb_binary,
        f"force install 'http://community-extensions.duckdb.org/v1.4.0/{platform}/capi_quack.duckdb_extension.gz'",
    )
    result = run_duckdb_query_json(
        duckdb_binary,
        "load capi_quack; SELECT extension_name, loaded FROM duckdb_extensions() WHERE extension_name='capi_quack'",
    )
    if result and len(result) > 0 and result[0]['loaded'] is True:
        print_success(f"{new_version} compatible with v1.4.0 CAPI")
        return

    print_error(f"Error installing v1.4.0 CAPI in {new_version}")


def main():
    parser = argparse.ArgumentParser(description='Check DuckDB release binary.')
    parser.add_argument('--binary', required=True, help='Path to the DuckDB binary to test')
    parser.add_argument('--prev-version', help='Previous DuckDB version (e.g., v1.2.0)', default='v1.2.0')
    parser.add_argument('--current-version', required=True, help='Current DuckDB version to be released')
    parser.add_argument('--current-hash', help='Current DuckDB hash to be released')
    parser.add_argument('--platform', required=True, help='Extension platform (e.g., linux_amd64)')

    args = parser.parse_args()

    settings = Settings(
        binary=args.binary,
        prev_version=args.prev_version,
        current_version=args.current_version,
        current_hash=args.current_hash,
        platform=args.platform,
    )

    binary_info = BinaryInfo(args.binary)

    print_step("1. Version & Hash Verification")
    check_version(settings, binary_info)

    print_step("2. Extension Autoloading")
    extension_autoloading(settings.binary)

    print_step("3. Secret Compatibility")
    secret_compatibility(settings.binary)

    print_step("4. Database Version Compatability")
    database_version_compatability(settings.binary, settings.current_version)

    print_step("5. CAPI Extension Cross-Version Compatibility")
    capi_compatibility(settings.binary, settings.current_version, settings.platform)

    if FAILURE:
        print()
        print_error(f"FAILURES DETECTED.", indent=0)
        sys.exit(1)


if __name__ == "__main__":
    main()
