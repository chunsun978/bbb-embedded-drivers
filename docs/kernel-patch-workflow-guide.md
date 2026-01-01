# Kernel Patch Workflow Guide

**Date:** December 27, 2024  
**Goal:** Learn professional Linux kernel contribution workflow and coding standards

---

## Overview

This guide covers the complete workflow for creating, formatting, and managing kernel patches. Even if you never submit upstream, these practices demonstrate professional-level kernel development skills.

### What You'll Learn

- Kernel coding style and conventions
- Using `scripts/checkpatch.pl` to validate code
- Creating well-formatted git commits
- Generating patches with `git format-patch`
- Patch series management
- Cover letters for patch sets
- (Optional) Upstream submission process

---

## Why This Matters

### Career Value

**On your resume:**
- "Developed Linux kernel drivers following mainline coding standards"
- "Created patch series with proper commit messages and cover letters"
- "Validated code with checkpatch.pl (zero warnings)"

**In interviews:**
- Shows you understand professional kernel development
- Demonstrates attention to detail
- Proves you can work in large collaborative projects

### Code Quality

Following kernel standards improves:
- ✅ **Readability** - Consistent style across all kernel code
- ✅ **Maintainability** - Standard patterns make code easier to understand
- ✅ **Correctness** - Coding rules catch common bugs
- ✅ **Performance** - Best practices baked into style guide

---

## Part 1: Kernel Coding Style

### Rule 1: Indentation - Tabs, Not Spaces

**Rule:** Use tabs for indentation (8 characters wide), spaces for alignment.

```c
// CORRECT
static int probe(struct platform_device *pdev)
{
→       struct device *dev = &pdev->dev;  // Tab for indent
→       int ret;

→       ret = do_something(dev,
→       →       →          arg2,  // Tabs + spaces to align with opening paren
→       →       →          arg3);
}

// WRONG - spaces instead of tabs
static int probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;  // 4 spaces - WRONG!
    int ret;
}
```

**Check your settings:**
```bash
# In vim:
:set tabstop=8
:set noexpandtab

# In VSCode: .editorconfig
[*.{c,h}]
indent_style = tab
indent_size = 8
tab_width = 8
```

### Rule 2: Line Length - 80 Columns Preferred, 100 Max

```c
// CORRECT - under 80 columns
static int my_function(struct device *dev)
{
    dev_err(dev, "error occurred\n");
}

// ACCEPTABLE - under 100 columns for readability
static int my_function(struct device *dev, struct resource *res, unsigned long flags)
{
    dev_err(dev, "failed to allocate resource at 0x%lx\n", res->start);
}

// WRONG - over 100 columns
static int my_function(struct device *dev)
{
    dev_err(dev, "this is a very long error message that goes way beyond 100 columns and should be split\n");
}

// CORRECT - split long lines
static int my_function(struct device *dev)
{
    dev_err(dev,
            "this is a very long error message split properly\n");
}
```

### Rule 3: Braces - K&R Style

```c
// CORRECT - opening brace on same line (except functions)
if (condition) {
    do_something();
} else {
    do_other();
}

// CORRECT - single statement can omit braces
if (condition)
    do_something();

// WRONG - opening brace on new line (BSD style)
if (condition)
{
    do_something();
}

// FUNCTIONS - opening brace on new line
static int my_func(void)
{
    return 0;
}
```

### Rule 4: Naming Conventions

```c
// Variables and functions - lowercase with underscores
static int button_count;
static int gpio_request_irq(struct device *dev);

// Macros and constants - UPPERCASE
#define MAX_BUTTONS 10
#define BUTTON_IRQ_FLAG (IRQF_TRIGGER_FALLING | IRQF_ONESHOT)

// Struct names - lowercase with underscores
struct button_device {
    int count;
};

// WRONG - camelCase
static int buttonCount;  // Wrong!
static int GpioRequestIrq(void);  // Wrong!
```

### Rule 5: Comments - C89 Style

```c
// CORRECT - C89 style comments
/*
 * Multi-line comment explaining the function.
 * Second line continues with star alignment.
 */
static int my_func(void)
{
    int x;  /* Inline comment */
}

// WRONG - C99 style (only allowed in certain contexts)
// This is a C99 comment - avoid in kernel code
```

### Rule 6: Variable Declarations

```c
// CORRECT - declare at beginning of block
static int my_func(void)
{
    int ret;
    unsigned long flags;
    struct device *dev;
    
    // ... code here ...
}

// WRONG - C99 style declarations anywhere
static int my_func(void)
{
    do_something();
    
    int ret;  // WRONG - must be at top of function!
}
```

### Rule 7: Pointer Declarations

```c
// CORRECT - asterisk attached to variable name
char *buf;
struct device *dev;

// WRONG - asterisk attached to type
char* buf;
struct device* dev;
```

---

## Part 2: Using checkpatch.pl

### What is checkpatch.pl?

A Perl script in the kernel source tree that validates code against kernel coding standards.

**Located at:** `/path/to/linux/scripts/checkpatch.pl`

### Running checkpatch.pl

**On a file:**
```bash
cd /home/chun/yocto/beaglebone/build/tmp/work-shared/beaglebone/kernel-source

./scripts/checkpatch.pl --file \
    /home/chun/projects/buildBBBWithYocto/kernel/drivers/button/bbb_flagship_button.c
```

**On a patch:**
```bash
git format-patch -1  # Create patch from last commit
./scripts/checkpatch.pl 0001-*.patch
```

**Common options:**
```bash
--file           # Check a source file (not patch)
--no-tree        # Don't check if file is in kernel tree
--strict         # Extra strict checking
--max-line-length=100  # Override 80 col limit
--ignore=CAMELCASE     # Ignore specific warnings
```

### Example Output

**Good code:**
```
total: 0 errors, 0 warnings, 150 lines checked

/path/to/file.c has no obvious style problems and is ready for submission.
```

**Code with issues:**
```bash
ERROR: code indent should use tabs where possible
#45: FILE: bbb_flagship_button.c:45:
+    int ret;$
     ^--- spaces instead of tabs

WARNING: line length of 105 exceeds 80 columns
#67: FILE: bbb_flagship_button.c:67:
+    dev_err(&pdev->dev, "this is a very long error message that exceeds the limit\n");

total: 1 errors, 1 warnings, 150 lines checked
```

### Fixing checkpatch Issues

1. **Run checkpatch:**
```bash
./scripts/checkpatch.pl --file --no-tree \
    ~/projects/buildBBBWithYocto/kernel/drivers/button/bbb_flagship_button.c \
    > /tmp/checkpatch.log 2>&1

cat /tmp/checkpatch.log
```

2. **Fix issues one by one**
3. **Re-run until clean**
4. **Commit the fixes**

---

## Part 3: Git Commit Messages

### Commit Message Format

```bash
<subsystem>: <short description (max 50 chars)>

<detailed explanation wrapped at 72 columns>

Additional paragraphs as needed.

Signed-off-by: Your Name <your.email@example.com>
```

### Examples

**Good commit message:**
```c
gpio: bbb-flagship-button: Add character device interface

Add /dev/bbb-button character device to allow userspace applications
to block on button events rather than polling sysfs. This provides
lower latency and better power efficiency.

The implementation uses wait queues for blocking I/O and maintains
compatibility with the existing sysfs interface.

Tested on BeagleBone Black with custom button hardware.

Signed-off-by: Chun Sun <your.email@example.com>
```

**Bad commit message:**
```
fix button driver

added char device stuff
```

### Commit Message Rules

1. **First line: 50 chars max**
   - Start with subsystem/module prefix
   - Use imperative mood ("Add", not "Added" or "Adds")
   - No period at end

2. **Blank line after first line**

3. **Body: 72 columns max**
   - Explain WHAT and WHY, not HOW
   - Be specific about testing

4. **Sign-off required**
   - `Signed-off-by: Your Name <email>`
   - Certifies you wrote it or have right to pass it on

### Setting Up Git

```bash
# Configure name and email
git config user.name "Chun Sun"
git config user.email "your.email@example.com"

# Set editor for commit messages
git config core.editor vim

# Set up commit template (optional)
cat > ~/.gitmessage.txt <<'EOF'
<subsystem>: <short description>

<Detailed explanation>

Signed-off-by: Chun Sun <your.email@example.com>
EOF

git config commit.template ~/.gitmessage.txt
```

---

## Part 4: Creating Patches

### Single Patch

**Create patch from last commit:**
```bash
git format-patch -1
# Creates: 0001-subsystem-short-description.patch
```

**Create patch from working directory (uncommitted changes):**
```bash
git diff > my-changes.patch
```

**Review patch before sending:**
```bash
cat 0001-*.patch
```

### Patch Format

```c
From 1234567890abcdef Mon Sep 17 00:00:00 2001
From: Chun Sun <your.email@example.com>
Date: Thu, 26 Dec 2024 10:00:00 -0800
Subject: [PATCH] gpio: bbb-flagship-button: Add character device interface

Add /dev/bbb-button character device to allow userspace applications
to block on button events rather than polling sysfs.

Tested on BeagleBone Black with custom button hardware.

Signed-off-by: Chun Sun <your.email@example.com>
---
 drivers/gpio/bbb_flagship_button.c | 50 +++++++++++++++++++++++++++++++
 1 file changed, 50 insertions(+)

diff --git a/drivers/gpio/bbb_flagship_button.c b/drivers/gpio/bbb_flagship_button.c
index 1234567..abcdefg 100644
--- a/drivers/gpio/bbb_flagship_button.c
+++ b/drivers/gpio/bbb_flagship_button.c
@@ -100,6 +100,10 @@ static int button_probe(struct platform_device *pdev)
+       /* Register character device */
+       ret = chardev_register(btn);
...
```

---

## Part 5: Patch Series

### Creating a Series

When you have multiple related commits:

```bash
# Create patches for last 3 commits
git format-patch -3

# Output:
# 0001-gpio-add-basic-button-driver.patch
# 0002-gpio-add-irq-handling.patch
# 0003-gpio-add-character-device.patch
```

### Cover Letter

For series with 2+ patches, create a cover letter:

```bash
git format-patch -3 --cover-letter

# Creates:
# 0000-cover-letter.patch  ← Edit this
# 0001-gpio-add-basic-button-driver.patch
# 0002-gpio-add-irq-handling.patch
# 0003-gpio-add-character-device.patch
```

**Edit cover letter:**
```bash
Subject: [PATCH 0/3] Add BeagleBone Black button driver

This series adds a platform driver for GPIO buttons on BeagleBone Black,
with IRQ handling and character device interface.

Patch 1 implements the basic platform driver with sysfs interface.
Patch 2 adds interrupt handling with software debouncing.
Patch 3 adds character device for blocking I/O.

Tested on BeagleBone Black Rev C with custom button on P8_07.

Chun Sun (3):
  gpio: Add basic button platform driver
  gpio: Add IRQ handling with debounce
  gpio: Add character device interface

 drivers/gpio/Kconfig               |  10 ++
 drivers/gpio/Makefile              |   1 +
 drivers/gpio/bbb_flagship_button.c | 350 +++++++++++++++++++++++++++++
 3 files changed, 361 insertions(+)
 create mode 100644 drivers/gpio/bbb_flagship_button.c
```

---

## Part 6: Validation Workflow

### Step-by-Step Process

**1. Make changes and test:**
```bash
# Edit code
vim kernel/drivers/button/bbb_flagship_button.c

# Build and test
./scripts/fast-build.sh button-deploy

# Test on BBB
ssh root@192.168.86.21 'cat /dev/bbb-button'
```

**2. Run checkpatch before committing:**
```bash
cd /home/chun/yocto/beaglebone/build/tmp/work-shared/beaglebone/kernel-source

# Check your modified file
./scripts/checkpatch.pl --file --no-tree \
    /home/chun/projects/buildBBBWithYocto/kernel/drivers/button/bbb_flagship_button.c

# Fix any errors or warnings
# Re-run until clean
```

**3. Commit with proper message:**
```bash
cd /home/chun/projects/buildBBBWithYocto

git add kernel/drivers/button/bbb_flagship_button.c
git commit

# Write commit message following format:
# gpio: bbb-flagship-button: Fix atomic context violation
#
# copy_to_user() was called while holding spinlock with IRQs disabled.
# Fixed by copying to local buffer first, releasing lock, then copying
# to userspace.
#
# Tested on BeagleBone Black Rev C.
#
# Signed-off-by: Chun Sun <your.email@example.com>
```

**4. Generate patch:**
```bash
git format-patch -1
# Creates: 0001-gpio-bbb-flagship-button-Fix-atomic-context-violation.patch
```

**5. Validate the patch:**
```bash
cd /home/chun/yocto/beaglebone/build/tmp/work-shared/beaglebone/kernel-source

./scripts/checkpatch.pl /home/chun/projects/buildBBBWithYocto/0001-*.patch
```

**6. Store validated patches:**
```bash
# Create patches directory
mkdir -p /home/chun/projects/buildBBBWithYocto/patches

# Move validated patches there
mv 0001-*.patch patches/
```

---

## Part 7: Common checkpatch Warnings

### WARNING: line over 80 characters

**Recommended:** Keep lines under 80 columns for readability.

**Fix:**
```c
// Before
dev_err(&pdev->dev, "failed to request IRQ %d for button on GPIO %d\n", irq, gpio);

// After
dev_err(&pdev->dev, "failed to request IRQ %d for button on GPIO %d\n",
        irq, gpio);
```

### ERROR: code indent should use tabs

**Fix:** Replace spaces with tabs. In vim:
```vim
:set tabstop=8
:set noexpandtab
:retab
```

### WARNING: prefer 'unsigned int' to 'unsigned'

```c
// Before
unsigned len;

// After
unsigned int len;
```

### WARNING: Prefer 'unsigned int' to bare use of 'unsigned'

```c
// Before  
static unsigned get_value(void);

// After
static unsigned int get_value(void);
```

### WARNING: Missing blank line after declarations

```c
// Before
static int probe(void)
{
    int ret;
    ret = do_something();
}

// After
static int probe(void)
{
    int ret;
    
    ret = do_something();  // Blank line after declarations
}
```

### WARNING: Prefer using '"%s...", __func__' to printk function name

```c
// Before
pr_info("my_func: starting\n");

// After
pr_info("%s: starting\n", __func__);
```

### ERROR: trailing whitespace

Remove spaces/tabs at end of lines. In vim:
```vim
:%s/\s\+$//g
```

---

## Part 8: Practical Exercise - Clean Up Your Code

### Exercise 1: Run checkpatch on Your Drivers

```bash
cd /home/chun/yocto/beaglebone/build/tmp/work-shared/beaglebone/kernel-source

# Check button driver
./scripts/checkpatch.pl --file --no-tree \
    ~/projects/buildBBBWithYocto/kernel/drivers/button/bbb_flagship_button.c \
    | tee ~/checkpatch-button.log

# Check chardev
./scripts/checkpatch.pl --file --no-tree \
    ~/projects/buildBBBWithYocto/kernel/drivers/button/bbb_flagship_button_chardev.c \
    | tee ~/checkpatch-chardev.log

# Check TMP117 driver
./scripts/checkpatch.pl --file --no-tree \
    ~/projects/buildBBBWithYocto/kernel/drivers/tmp117/bbb_tmp117.c \
    | tee ~/checkpatch-tmp117.log
```

### Exercise 2: Fix All Issues

Go through each warning/error and fix it. Common fixes:
- Convert spaces to tabs
- Split long lines
- Add blank lines after declarations
- Fix comment style
- Remove trailing whitespace

### Exercise 3: Create Clean Commit

```bash
# After fixing all checkpatch issues
git add kernel/drivers/button/bbb_flagship_button.c
git commit -m "gpio: bbb-flagship-button: Clean up coding style

Fix checkpatch.pl warnings:
- Convert indentation to tabs
- Split lines exceeding 80 columns
- Add blank lines after declarations
- Fix comment formatting

No functional changes.

Signed-off-by: Chun Sun <your.email@example.com>"
```

### Exercise 4: Generate and Validate Patch

```bash
# Generate patch
git format-patch -1

# Validate it
cd /home/chun/yocto/beaglebone/build/tmp/work-shared/beaglebone/kernel-source
./scripts/checkpatch.pl ~/projects/buildBBBWithYocto/0001-*.patch

# Should show: "total: 0 errors, 0 warnings"
```

---

## Part 9: Organizing Your Patch History

### Recommended Git Structure

```bash
patches/
├── v1/
│   ├── 0001-gpio-add-button-driver.patch
│   ├── 0002-gpio-add-irq-handling.patch
│   └── 0003-gpio-add-chardev.patch
├── v2/  (after review/fixes)
│   ├── 0001-gpio-add-button-driver.patch
│   ├── 0002-gpio-add-irq-handling.patch
│   └── 0003-gpio-add-chardev.patch
└── README.md  (patch changelog)
```

### Changelog Example

**patches/README.md:**
```markdown
# Button Driver Patch Series

## v2 (2024-12-27)
- Fixed atomic context violation in read()
- Added null-termination to event buffer
- Cleaned up coding style (0 checkpatch warnings)
- Improved commit messages

## v1 (2024-12-26)
- Initial implementation
- Had checkpatch warnings (tabs, line length)
```

---

## Part 10: Professional Patch Checklist

Before submitting or archiving a patch:

- [ ] Code works correctly (tested on hardware)
- [ ] `checkpatch.pl` shows 0 errors, 0 warnings
- [ ] Commit message follows format (50 char subject, 72 char body)
- [ ] Signed-off-by line present
- [ ] No commented-out code
- [ ] No debug printks (unless intentional)
- [ ] No trailing whitespace
- [ ] Proper indentation (tabs)
- [ ] Follows kernel naming conventions
- [ ] Includes testing notes in commit message

---

## Part 11: Advanced - Upstream Submission (Optional)

If you want to submit to mainline kernel:

### Step 1: Find Maintainers

```bash
cd /home/chun/yocto/beaglebone/build/tmp/work-shared/beaglebone/kernel-source

./scripts/get_maintainer.pl \
    ~/projects/buildBBBWithYocto/0001-*.patch

# Output:
# Linus Walleij <linus.walleij@linaro.org> (maintainer:GPIO SUBSYSTEM)
# linux-gpio@vger.kernel.org (open list:GPIO SUBSYSTEM)
# linux-kernel@vger.kernel.org (open list)
```

### Step 2: Subscribe to Mailing List

- Visit: https://vger.kernel.org/vger-lists.html
- Subscribe to relevant list (e.g., linux-gpio)
- Read the list for a few days to understand tone/expectations

### Step 3: Send Patch

```bash
git send-email --to=linux-gpio@vger.kernel.org \
               --cc=maintainer@example.com \
               0001-*.patch
```

**Note:** For your custom BBB driver, upstream submission isn't expected. But the workflow is valuable to know!

---

## Part 12: Your Driver's Patch Series Plan

### Proposed Patch Series

**Patch 1: Core platform driver**
```bash
gpio: Add BeagleBone Black flagship button driver

Add platform driver for GPIO button on BeagleBone Black with:
- Device tree binding
- GPIO acquisition via consumer API
- IRQ handling with software debouncing
- sysfs interface for press counts and timestamps

Hardware: Custom button on P8_07 (GPIO2_2)
Tested: BeagleBone Black Rev C with 6.1.80-ti kernel
```

**Patch 2: Debug counters**
```bash
gpio: bbb-flagship-button: Add debug counters

Add debug counters for IRQ and work execution tracking:
- total_irqs: All IRQs including bounces
- work_executions: Debounce work runs

These help developers tune debounce parameters and understand
button bounce characteristics.
```

**Patch 3: Character device**
```bash
gpio: bbb-flagship-button: Add character device interface

Add /dev/bbb-button character device for efficient event delivery.
Provides blocking I/O with wait queues, eliminating the need for
userspace polling.

Uses local buffer pattern to avoid copy_to_user() in atomic context.
Maintains compatibility with existing sysfs interface.
```

### Create This Series

```bash
cd /home/chun/projects/buildBBBWithYocto

# Organize commits (if needed - rebase interactive)
git rebase -i HEAD~10

# Generate series with cover letter
git format-patch -3 --cover-letter -o patches/v1/

# Validate each patch
for p in patches/v1/*.patch; do
    ~/yocto/beaglebone/build/tmp/work-shared/beaglebone/kernel-source/scripts/checkpatch.pl "$p"
done
```

---

## Part 13: Quick Reference

### Useful Scripts

```bash
# Check single file
checkpatch.pl --file --no-tree file.c

# Check patch
checkpatch.pl patch.patch

# Check last commit as patch
git format-patch -1 --stdout | checkpatch.pl -

# Generate patches for last N commits
git format-patch -N

# Generate with cover letter
git format-patch -N --cover-letter
```

### Git Aliases

Add to `~/.gitconfig`:

```ini
[alias]
    # Create patch from last commit
    mkpatch = format-patch -1
    
    # Create patch series with cover
    mkseries = format-patch --cover-letter -o patches/
    
    # Check current changes with checkpatch
    checkpatch = !git diff | ~/kernel/scripts/checkpatch.pl --no-tree -
```

---

## Part 14: Documentation Patches

### If Adding New Driver

You should also include documentation patches:

**Patch 1/4: Add driver**
**Patch 2/4: Add device tree bindings doc**
**Patch 3/4: Update MAINTAINERS**
**Patch 4/4: Add to Kconfig/Makefile**

**Device tree bindings doc:**

```yaml
# Documentation/devicetree/bindings/gpio/bbb-flagship-button.yaml

%YAML 1.2
---
$id: http://devicetree.org/schemas/gpio/bbb-flagship-button.yaml#
$schema: http://devicetree.org/meta-schemas/core.yaml#

title: BeagleBone Black Flagship Button Driver

maintainers:
  - Chun Sun <your.email@example.com>

properties:
  compatible:
    const: bbb,flagship-button

  button-gpios:
    maxItems: 1
    description: GPIO for button input

  debounce-ms:
    $ref: /schemas/types.yaml#/definitions/uint32
    description: Software debounce time in milliseconds
    default: 20

required:
  - compatible
  - button-gpios

examples:
  - |
    button {
        compatible = "bbb,flagship-button";
        button-gpios = <&gpio2 2 GPIO_ACTIVE_LOW>;
        debounce-ms = <20>;
    };
```

---

## Summary

### Skills You'll Master

✅ **Kernel coding style** - Industry standard formatting  
✅ **checkpatch.pl** - Automated validation  
✅ **Git commit messages** - Professional format  
✅ **Patch creation** - Single patches and series  
✅ **Code review preparation** - Self-review before submission  

### Workflow

```
Code → Test → checkpatch → Fix → Commit → format-patch → Validate → Archive
```

### Time Investment

- **Initial learning:** 2-3 hours
- **First cleanup pass:** 1-2 hours per driver
- **Ongoing:** 5-10 minutes per change (becomes automatic)

### Deliverable

After completing this:
- Clean, maintainable code (zero checkpatch warnings)
- Professional git history with proper commit messages
- Patch series ready for portfolio or upstream submission
- Demonstrated mastery of kernel development practices

---

## Next Steps

1. **Run checkpatch on all your drivers** - See what needs fixing
2. **Create a cleanup branch** - Fix style issues without changing functionality
3. **Generate patch series** - Create v1 patches for your portfolio
4. **Document in README** - Show clean checkpatch results
5. **Move to Option 1** - Input subsystem driver (next major feature)

---

**Document Version:** 1.0  
**Author:** Chun  
**Project:** BeagleBone Black Flagship Driver Development  
**Date:** December 27, 2024

