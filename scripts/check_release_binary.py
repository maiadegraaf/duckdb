import os
import subprocess
import sys
import argparse
import json
import re


def print_section(title):
    print(f"\n--- {title} ---")


def run_command(command, shell=True, env=None, capture_output=False):
    if not capture_output:
        print(f"Running: {command}")
        process = subprocess.Popen(
            command, shell=shell, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env
        )
        for line in process.stdout:
            print(line, end='')
        process.wait()
        if process.returncode != 0:
            print(f"Command failed with exit code {process.returncode}")
            sys.exit(process.returncode)
        return None
    else:
        result = subprocess.run(command, shell=shell, capture_output=True, text=True, env=env)
        if result.returncode != 0:
            print(f"Command failed with exit code {result.returncode}")
            print(f"STDOUT: {result.stdout}")
            print(f"STDERR: {result.stderr}")
            sys.exit(result.returncode)
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


def get_expected_codename(lib_version, repo_root=None):
    if "-dev" in lib_version:
        return "Development Version"

    if repo_root:
        source_path = os.path.join(repo_root, 'src', 'function', 'table', 'version', 'pragma_version.cpp')
    else:
        source_path = os.path.join(
            os.path.dirname(__file__), '..', 'src', 'function', 'table', 'version', 'pragma_version.cpp'
        )

    if not os.path.exists(source_path):
        if "-dev" in lib_version:
            return "Development Version"
        print(f"Error: Could not find {source_path} to parse codename.")
        sys.exit(1)

    try:
        with open(source_path, 'r') as f:
            content = f.read()

        # Look for the ReleaseCodename function
        match = re.search(r'const char \*DuckDB::ReleaseCodename\(\) \{(.*?)\}', content, re.DOTALL)
        if not match:
            return "Unknown Version"

        body = match.group(1)
        # Find all StartsWith checks
        # e.g., if (StringUtil::StartsWith(DUCKDB_VERSION, "v1.2.")) { return "Histrionicus"; }
        versions = re.findall(r'StringUtil::StartsWith\(DUCKDB_VERSION, "(.*?)"\)\) \{\s*return "(.*?)";', body)
        for prefix, codename in versions:
            if lib_version.startswith(prefix):
                return codename

    except Exception as e:
        print(f"Warning: Error parsing {source_path}: {e}")

    return "Unknown Version"


def check_version(args):
    print_section("1. Version & Hash Verification")
    version_info = run_duckdb_query_json(args.binary, "PRAGMA version;")[0]
    lib_version = version_info['library_version']
    source_id = version_info['source_id']
    codename = version_info['codename']

    if lib_version != args.current_version:
        print(f"Error: Version mismatch! Expected {args.current_version}, got {lib_version}")
    else:
        print(f"Version Match: {lib_version} ✅")

    expected_codename = get_expected_codename(lib_version, args.repo_root)

    if codename != expected_codename:
        print(f"Error: Codename mismatch! Expected {expected_codename} for version {lib_version}, got {codename}")
        sys.exit(1)
    else:
        print(f"Codename match: {codename} ✅")

    if not source_id or len(source_id) < 7:
        print(f"Error: Invalid Source ID (hash): {source_id}")
        sys.exit(1)

    if args.current_hash:
        if not source_id.startswith(args.current_hash) and not args.current_hash.startswith(source_id):
            print(f"Error: Hash mismatch! Expected (part of) {args.current_hash}, got {source_id}")
            sys.exit(1)
        print(f"Hash Match: {source_id} ✅")

    return version_info


def main():
    parser = argparse.ArgumentParser(description='Check DuckDB release binary.')
    parser.add_argument('--binary', required=True, help='Path to the DuckDB binary to test')
    parser.add_argument('--prev-version', help='Previous DuckDB version (e.g., v1.2.0)')
    parser.add_argument('--current-version', required=True, help='Current DuckDB version to be released')
    parser.add_argument('--current-hash', help='Current DuckDB hash to be released')
    parser.add_argument('--repo-root', help='Path to the DuckDB repository root')
    parser.add_argument('--platform', required=True, help='Extension platform (e.g., linux_amd64)')
    parser.add_argument('--python-venv', help='Path to the python executable in the venv')

    args = parser.parse_args()

    binary = args.binary
    prev_version = args.prev_version
    current_version = args.current_version
    current_hash = args.current_hash
    platform = args.platform
    python_venv = args.python_venv

    version_info = check_version(args)
    lib_version = version_info['library_version']

    print("\n--- 2. Extension Autoloading & Versioning ---")
    # We use -c here to ensure autoloading is triggered by the query
    # We also explicitly enable autoloading to be sure
    query = "SET autoload_known_extensions=1; SET s3_region='us-east-1'; SELECT extension_name, extension_version, loaded FROM duckdb_extensions() WHERE extension_name='httpfs'"
    extensions_info = run_duckdb_query_json(binary, query)
    if not extensions_info:
        print("Error: httpfs extension not found in duckdb_extensions()")
        sys.exit(1)

    httpfs = extensions_info[0]
    print(f"Extension: {httpfs['extension_name']}, Version: {httpfs['extension_version']}, Loaded: {httpfs['loaded']}")

    if not httpfs['loaded']:
        print("Error: httpfs extension failed to autoload!")
        sys.exit(1)

    # Core extensions should usually match the library version
    if httpfs['extension_version'] != lib_version:
        # For non-dev releases, the extension version might be just the version without 'v'
        expected_ext_version = lib_version.lstrip('v')
        if httpfs['extension_version'] != expected_ext_version:
            print(
                f"Error: Extension version mismatch! Expected {lib_version} or {expected_ext_version}, got {httpfs['extension_version']}"
            )
            sys.exit(1)

    if prev_version and python_venv:
        print("\n--- 3. Secret Compatibility ---")
        # Create secret with old version
        run_command(
            f"{python_venv} -c \"import duckdb;duckdb.query(\\\"CREATE OR REPLACE PERSISTENT SECRET test_secret (TYPE s3, KEY_ID 'fake', SECRET 'alsofake')\\\").show()\""
        )
        # Read with new version
        run_duckdb_query(binary, "select secret_string from duckdb_secrets();")

    print("\n--- 4. Database Version Compatibility ---")

    # 4.1 Forward compatibility: Create with new version, read with new version
    db_latest = "test_latest.db"
    if os.path.exists(db_latest):
        os.remove(db_latest)
    run_duckdb_query(
        binary,
        f"ATTACH '{db_latest}' as t (STORAGE_VERSION '{current_version}'); CREATE TABLE t.test AS SELECT 'success!' as a;",
    )
    run_duckdb_query(binary, f"ATTACH '{db_latest}' as t; SELECT * FROM t.test;")

    if prev_version and python_venv:
        # 4.2 Backward compatibility (Old storage version): Create with new binary specifying old storage version, read with old binary
        db_old_storage = "test_old_storage.db"
        if os.path.exists(db_old_storage):
            os.remove(db_old_storage)
        run_duckdb_query(
            binary,
            f"ATTACH '{db_old_storage}' as t (STORAGE_VERSION '{prev_version}'); CREATE TABLE t.test AS SELECT 'success!' as a;",
        )
        run_command(
            f"{python_venv} -c \"import duckdb;con = duckdb.connect('{db_old_storage}'); print(con.execute('SELECT * FROM test').fetchall())\""
        )

        # 4.3 Backward compatibility (Old file): Create with old binary, read with new binary
        db_old_file = "test_old_file.db"
        if os.path.exists(db_old_file):
            os.remove(db_old_file)
        run_command(
            f"{python_venv} -c \"import duckdb; con = duckdb.connect('{db_old_file}'); con.execute(\\\"CREATE TABLE test AS SELECT 'success!' as a\\\");\""
        )
        run_duckdb_query(binary, f"ATTACH '{db_old_file}' as t; SELECT * FROM t.test;")

    if prev_version:
        print("\n--- 5. Test CAPI extension cross-version compatibility ---")
        run_duckdb_query(
            binary,
            f"force install 'http://community-extensions.duckdb.org/{prev_version}/{platform}/capi_quack.duckdb_extension.gz'",
        )
        run_duckdb_query(
            binary,
            "load capi_quack; SELECT extension_name, loaded FROM duckdb_extensions() WHERE extension_name='capi_quack'",
        )

    print("\nAll checks passed successfully!")


if __name__ == "__main__":
    main()
