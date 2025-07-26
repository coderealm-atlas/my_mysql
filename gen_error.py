#!/usr/bin/env python3

import configparser
import argparse
import os

def parse_ini(filepath):
    config = configparser.ConfigParser()
    config.optionxform = str  # Preserve case
    config.read(filepath)
    return config

def generate_header(config, filename):
    out = []
    out.append(f"// Auto-generated from {os.path.basename(filename)}\n#pragma once\n")
    out.append("namespace db_errors {\n")

    # Generate only constexpr int values (no enum)
    for section in config.sections():
        out.append(f"// {section} error codes")
        for name, value in config[section].items():
            code, *msg = value.split(",", 1)
            out.append(f"constexpr int {name} = {code.strip()};")
        out.append("")

    out.append("}  // namespace db_errors")
    return "\n".join(out)

def main():
    parser = argparse.ArgumentParser(description="Generate error_codes.hpp from .ini")
    parser.add_argument("--input", "-i", required=True, help="Input .ini file")
    parser.add_argument("--output", "-o", default="include/db_errors.hpp", help="Output .hpp file")
    args = parser.parse_args()

    config = parse_ini(args.input)
    header_code = generate_header(config, args.input)

    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    with open(args.output, "w") as f:
        f.write(header_code)

    print(f"✅ Generated: {args.output}")

if __name__ == "__main__":
    main()

# #!/usr/bin/env python3

# import configparser
# import argparse
# import os

# def parse_ini(filepath):
#     """Parse the .ini file while preserving case sensitivity."""
#     config = configparser.ConfigParser()
#     config.optionxform = str  # Preserve case for names
#     config.read(filepath)
#     return config

# def generate_header(config, filename):
#     """Generate the C++ header file with enum and constexpr values."""
#     out = []
#     out.append(f"// Auto-generated from {os.path.basename(filename)}\n#pragma once\n")
#     out.append("#include <cstdint>\n\nnamespace db_errors {\n")

#     # Generate constexpr ints and enum class for each section
#     for section in config.sections():
#         # Generate constexpr int values first
#         out.append(f"// {section} error codes")
#         for name, value in config[section].items():
#             code, *_ = value.split(",", 1)  # Extract just the numeric code
#             out.append(f"constexpr int {name}_int = {code.strip()};")
#         out.append("")

#         # Generate the enum class
#         out.append(f"enum class {section} : int {{")
#         for name in config[section]:
#             out.append(f"    {name} = {name}_int,")
#         out.append("};\n")

#         # Generate to_int helper
#         out.append(f"inline int to_int({section} e) {{ return static_cast<int>(e); }}\n")

#     out.append("}  // namespace db_errors")
#     return "\n".join(out)

# def main():
#     parser = argparse.ArgumentParser(
#         description="Generate error_codes.hpp from error_codes.ini"
#     )
#     parser.add_argument(
#         "--input", "-i", 
#         required=True, 
#         help="Path to input .ini file (e.g., error_codes.ini)"
#     )
#     parser.add_argument(
#         "--output", "-o", 
#         default="include/db_errors.hpp",
#         help="Output .hpp file (default: include/db_errors.hpp)"
#     )

#     args = parser.parse_args()
#     config = parse_ini(args.input)
#     header_code = generate_header(config, args.input)

#     # Write the generated file
#     os.makedirs(os.path.dirname(args.output), exist_ok=True)
#     with open(args.output, "w") as f:
#         f.write(header_code)

#     print(f"✅ Generated: {args.output}")

# if __name__ == "__main__":
#     main()

# #!/usr/bin/env python3

# import configparser
# import argparse
# import os

# def parse_ini(filepath):
#     config = configparser.ConfigParser()
#     config.optionxform = str  # preserve case for names
#     config.read(filepath)
#     return config

# def generate_header(config, filename):
#     out = []
#     out.append(f"// Generated from {os.path.basename(filename)}\n#pragma once\n")
#     out.append("#include <string>\n#include <ostream>\n\nnamespace db_errors {\n")

#     # Enums
#     for section in config.sections():
#         out.append(f"enum class {section} : int {{")
#         for name, value in config[section].items():
#             code, *_ = value.split(",", 1)
#             out.append(f"  {name} = {code.strip()},")
#         out.append("};\n")

#     # to_int helpers
#     for section in config.sections():
#         out.append(f"inline int to_int({section} e) {{ return static_cast<int>(e); }}")

#     # Error struct
# #     out.append("""
# # struct Error {
# #   int code;
# #   std::string message;

# #   Error(int code_, std::string msg) : code(code_), message(std::move(msg)) {}
# # };

# # inline std::ostream& operator<<(std::ostream& os, const Error& e) {
# #   return os << "[Error " << e.code << "] " << e.message;
# # }
# # """)

#     out.append("}  // namespace error\n")
#     return "\n".join(out)

# def main():
#     parser = argparse.ArgumentParser(description="Generate error_codes.hpp from error_codes.ini")
#     parser.add_argument(
#       "--input", "-i", 
#       required=True, 
#       help="Path to input .ini file"
#     )
#     parser.add_argument(
#         "--output", "-o", 
#         default="include/db_errors.hpp",  # 👈 Default value added here
#         help="Path to output .hpp file (default: include/db_errors.hpp)"
#     )

#     args = parser.parse_args()
#     config = parse_ini(args.input)

#     header_code = generate_header(config, args.input)

#     with open(args.output, "w") as f:
#         f.write(header_code)

#     print(f"✅ Generated: {args.output}")

# if __name__ == "__main__":
#     main()
