from datetime import datetime
from pathlib import Path

Import("env")

build_dir = Path(env.subst("$BUILD_DIR"))
build_dir.mkdir(parents=True, exist_ok=True)

stamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
header = build_dir / "kmj_generated_build_stamp.hpp"
header.write_text(f'#pragma once\n#define KMJ_BUILD_TIMESTAMP "{stamp}"\n')

env.Append(CPPPATH=[str(build_dir)])
