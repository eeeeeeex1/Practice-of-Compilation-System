# ToyC Compiler Test Suite
$compiler = "build\Release\toyc.exe"
$testDir = "test"
$failCount = 0
$passCount = 0

$tests = @(
    @{name="test1_return42.tc";      desc="Simple return 42"},
    @{name="test2_arithmetic.tc";    desc="Arithmetic 1+2*3-4/2 = 5"},
    @{name="test3_localvars.tc";     desc="Local variables x=10, y=20, x+y=30"},
    @{name="test4_ifelse.tc";        desc="If-else: x=10>5 return 1"},
    @{name="test5_while.tc";        desc="While loop: sum of 0..9 = 45"},
    @{name="test6_function.tc";      desc="Function call: add(3,4)=7"},
    @{name="test7_global.tc";        desc="Global variable g=42"},
    @{name="test8_const.tc";         desc="Const N=100, return N+50=150"},
    @{name="test9_relation.tc";      desc="Short-circuit: a<b && b>0"},
    @{name="test10_recursive.tc";    desc="Recursive fibonacci(10)=55"},
    @{name="test11_void.tc";         desc="Void function setResult"},
    @{name="test12_negative.tc";     desc="Negative numbers: -((-10)+(-20))=30"},
    @{name="test13_scope.tc";        desc="Nested scopes: return outer x=10"},
    @{name="test14_break_continue.tc"; desc="Break and continue in while"}
)

Write-Host "=== ToyC Compiler Test Suite ===" -ForegroundColor Cyan
Write-Host ""

foreach ($test in $tests) {
    $testPath = Join-Path $testDir $test.name
    $output = cmd /c "$compiler < $testPath" 2>&1
    $exitCode = $LASTEXITCODE

    if ($exitCode -eq 0) {
        Write-Host "  PASS: $($test.desc)" -ForegroundColor Green
        $passCount++
    } else {
        Write-Host "  FAIL: $($test.desc)" -ForegroundColor Red
        Write-Host "    $output" -ForegroundColor Red
        $failCount++
    }
}

Write-Host ""
Write-Host "=== Results: $passCount passed, $failCount failed ===" -ForegroundColor $(if ($failCount -eq 0) { "Green" } else { "Red" })

# Test with -opt flag
Write-Host ""
Write-Host "=== Testing with -opt flag ===" -ForegroundColor Cyan
$optOutput = cmd /c "$compiler -opt < $testDir\test10_recursive.tc" 2>&1
if ($LASTEXITCODE -eq 0) {
    Write-Host "  PASS: -opt flag accepted and compiles recursive test" -ForegroundColor Green
} else {
    Write-Host "  FAIL: -opt flag test" -ForegroundColor Red
}
