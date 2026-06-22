# ToyC Compiler Test Suite
# Run from project root directory

$ErrorActionPreference = "Continue"

$compiler = "build\toyc.exe"
$testDir = "test"
$passCount = 0
$failCount = 0

# Test cases with expected return values
$tests = @(
    @{name="test1_return42.tc";      desc="Simple return 42";           expected=42},
    @{name="test2_arithmetic.tc";    desc="Arithmetic 1+2*3-4/2 = 5";   expected=5},
    @{name="test3_localvars.tc";     desc="Local variables x=10, y=20"; expected=30},
    @{name="test4_ifelse.tc";        desc="If-else: x=10>5 return 1";   expected=1},
    @{name="test5_while.tc";         desc="While loop: sum of 0..9";    expected=45},
    @{name="test6_function.tc";      desc="Function call: add(3,4)";    expected=7},
    @{name="test7_global.tc";        desc="Global variable g=42";       expected=42},
    @{name="test8_const.tc";         desc="Const N=100, return N+50";   expected=150},
    @{name="test9_relation.tc";      desc="Short-circuit: a<b && b>0";  expected=1},
    @{name="test10_recursive.tc";    desc="Recursive fibonacci(10)";    expected=55},
    @{name="test11_void.tc";         desc="Void function setResult";    expected=99},
    @{name="test12_negative.tc";     desc="Negative numbers: -((-10)+(-20))"; expected=30},
    @{name="test13_scope.tc";        desc="Nested scopes: outer x=10";  expected=10},
    @{name="test14_break_continue.tc"; desc="Break and continue in while"; expected=55}
)

Write-Host "=== ToyC Compiler Test Suite ===" -ForegroundColor Cyan
Write-Host ""

# Check if compiler exists
if (-not (Test-Path $compiler)) {
    Write-Host "Compiler not found: $compiler" -ForegroundColor Red
    Write-Host "Please build the compiler first using build.bat or g++" -ForegroundColor Yellow
    exit 1
}

# Run compilation tests
foreach ($test in $tests) {
    $testPath = Join-Path $testDir $test.name
    
    Write-Host "Testing: $($test.desc)" -NoNewline
    
    try {
        # Compile the test file
        $asmOutput = Get-Content $testPath | & $compiler 2>&1
        
        if ($asmOutput -match "error" -or $LASTEXITCODE -ne 0) {
            Write-Host " - FAIL (compilation error)" -ForegroundColor Red
            Write-Host "  Output: $asmOutput" -ForegroundColor Red
            $failCount++
        } else {
            # Compilation successful - save assembly
            $asmFile = "build\$($test.name).s"
            $asmOutput | Out-File -FilePath $asmFile -Encoding UTF8
            Write-Host " - PASS" -ForegroundColor Green
            $passCount++
        }
    } catch {
        Write-Host " - ERROR" -ForegroundColor Red
        Write-Host "  $_" -ForegroundColor Red
        $failCount++
    }
}

Write-Host ""
Write-Host "=== Compilation Results: $passCount passed, $failCount failed ===" -ForegroundColor $(if ($failCount -eq 0) { "Green" } else { "Red" })

# Test with -opt flag
Write-Host ""
Write-Host "=== Testing -opt flag ===" -ForegroundColor Cyan
$optTest = Join-Path $testDir "test10_recursive.tc"
try {
    $optOutput = Get-Content $optTest | & $compiler -opt 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  PASS: -opt flag accepted and compiles recursive test" -ForegroundColor Green
    } else {
        Write-Host "  FAIL: -opt flag test" -ForegroundColor Red
    }
} catch {
    Write-Host "  ERROR: $_" -ForegroundColor Red
}

Write-Host ""
Write-Host "Note: Full execution testing requires RISC-V toolchain (riscv32-unknown-elf-gcc, qemu-riscv32)" -ForegroundColor Yellow
Write-Host "The generated .s files can be assembled and linked with:" -ForegroundColor Yellow
Write-Host "  riscv32-unknown-elf-gcc -march=rv32i -mabi=ilp32 build/test.s -o build/test.elf" -ForegroundColor Yellow
Write-Host "  qemu-riscv32 build/test.elf; echo 'Return value:' \$?" -ForegroundColor Yellow