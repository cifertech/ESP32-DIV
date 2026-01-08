# **ESP32-DIV Contribution Plan: PlatformIO Migration & CI/CD Pipeline**

Https://github.com/cifertech/ESP32-DIV

**Objective:** Transition the ESP32-DIV project from a manual Arduino IDE workflow to a robust, automated PlatformIO environment. This will standardize dependencies, improve code quality via static analysis, and automate firmware release generation.

## **Phase 1: Environment Standardization (PlatformIO)**

### **Task Description**

Transition the project build system from Arduino IDE (.ino structure) to PlatformIO. This involves creating a platformio.ini configuration file that explicitly defines hardware targets, framework versions, and external library dependencies.

### **Technical Implementation Detail**

1. **Initialize Project**: Create a platformio.ini file in the root directory.  
2. **Define Environment**: Configure the \[env:esp32dev\] block (or appropriate board variant).  
3. **Framework**: Set framework \= arduino and platform \= espressif32.  
4. **Dependencies**: Identify all libraries currently installed manually in Arduino IDE (e.g., TFT\_eSPI, Adafruit\_NeoPixel, CC1101 libraries). Add them to lib\_deps using their library registry names or GitHub URLs.  
5. **Structure Refactor**:  
   * Ensure the main .ino file is compatible or rename/refactor main logic to src/main.cpp.  
   * Move header files to include/ and source files to src/ if not already structured effectively.

### **Verification/Testing**

* **Unit Test**: Run the command pio run locally in the terminal.  
* **Success Criteria**: The project compiles without fatal errors, and the generated firmware.bin matches the functional behavior of the Arduino IDE build when flashed to the device.

## **Phase 2: Static Code Analysis Integration**

### **Task Description**

Integrate static analysis tools to automatically detect memory leaks, buffer overflows, and uninitialized variables, which are critical in embedded C++ security tools.

### **Technical Implementation Detail**

1. **Configuration**: Add check\_tool \= cppcheck and check\_skip\_packages \= yes to platformio.ini.  
2. **Flags**: Configure check\_flags to enable strict checking:  
   check\_flags \=  
     cppcheck: \--enable=all \--inconclusive \--std=c++11 \--suppress=\*:\*.pio/\*

3. **Suppression**: Create a suppression list if external libraries generate excessive false positives, ensuring the focus remains on the project's source code.

### **Verification/Testing**

* **Integration Test**: Execute pio check within the project root.  
* **Success Criteria**: The tool generates an analysis report. The goal is to reach zero "Critical" or "High" severity errors in the src/ directory.

## **Phase 3: GitHub Actions Workflow (CI)**

### **Task Description**

Implement a GitHub Actions workflow to automate the build and verification process on every push to main and every Pull Request.

### **Technical Implementation Detail**

1. **Workflow File**: Create .github/workflows/build\_firmware.yml.  
2. **Triggers**: Set on: \[push, pull\_request\].  
3. **Job Steps**:  
   * **Checkout**: Use actions/checkout@v4.  
   * **Setup Python**: Use actions/setup-python@v4.  
   * **Install Core**: Run pip install platformio.  
   * **Build**: Run pio run.  
   * **Lint**: Run pio check \--fail-on-defect=high.

### **Verification/Testing**

* **System Test**: Push a commit to a new feature branch and open a Pull Request.  
* **Success Criteria**: The GitHub Action runner triggers automatically, successfully installs dependencies, builds the firmware, passes the linting stage, and reports a green "Success" status on the PR.

## **Phase 4: Automated Release Packaging (CD)**

### **Task Description**

Automate the generation of release assets when a new Git tag is pushed. This ensures users always have access to the latest compiled binaries without needing to set up a compile environment themselves.

### **Technical Implementation Detail**

1. **Workflow Extension**: Update .github/workflows/build\_firmware.yml to trigger on tags: tags: \['v\*'\].  
2. **Artifact Renaming**: Add a step after the build to rename .pio/build/esp32dev/firmware.bin to ESP32-DIV-${{ github.ref\_name }}.bin.  
3. **Release Action**: Use softprops/action-gh-release@v1 (or similar) to create a GitHub Release.  
4. **Upload**: Configure the action to upload the renamed binary file as an asset.

### **Verification/Testing**

* **Integration Test**: Push a test tag (e.g., git tag v1.0.0-test && git push origin v1.0.0-test).  
* **Success Criteria**: A new Release appears on the GitHub repository page containing the correctly named .bin file and the release notes derived from the commit history or tag description.