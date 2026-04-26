#!/usr/bin/env python3

import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path


class ConvertModuleBundleTest(unittest.TestCase):
    def test_xml_round_trip_preserves_interface_metadata(self):
        qt_root = Path(__file__).resolve().parents[1]
        tool = qt_root / "tools" / "convert_module_bundle.py"

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            input_path = temp_path / "modules.xml"
            output_dir = temp_path / "out"
            input_path.write_text(
                """<?xml version="1.0" encoding="UTF-8"?>
<module-bundle>
  <buses>
    <bus name="ni_link" description="Endpoint-to-router">
      <compatibility>
        <role name="initiator" connects_to="target" />
        <role name="target" connects_to="initiator" />
        <match field="protocol" />
      </compatibility>
    </bus>
  </buses>
  <module name="Endpoint">
    <interfaces>
      <interface id="noc" bus="ni_link" role="initiator" connects_to="target" match="protocol">
        <config field="protocol" parameter="protocol" />
      </interface>
    </interfaces>
    <ports>
      <port id="noc" direction="input" type="bus" bus_type="ni_link" role="attachment" name="NoC" interface="noc" />
    </ports>
  </module>
</module-bundle>
""",
                encoding="utf-8",
            )

            result = subprocess.run(
                [
                    sys.executable,
                    str(tool),
                    "--xml",
                    str(input_path),
                    "--output-dir",
                    str(output_dir),
                ],
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            root = ET.parse(output_dir / "modules.xml").getroot()
            self.assertIsNotNone(root.find("buses/bus[@name='ni_link']"))
            self.assertIsNotNone(root.find("module/interfaces/interface[@id='noc']"))
            self.assertEqual(
                root.find("module/ports/port[@id='noc']").get("interface"),
                "noc",
            )


if __name__ == "__main__":
    unittest.main()
