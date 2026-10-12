param([string]$Label, [string]$Dir)
$Dir = $Dir.TrimEnd('"', '\', ' ')
if (-not (Test-Path $Dir)) {
    Write-Output "PROBE $Label : directory missing: $Dir"
    exit 0
}
Get-ChildItem -LiteralPath $Dir -Recurse -File |
    ForEach-Object {
        Write-Output ("PROBE {0} : {1,10}  {2}" -f $Label, $_.Length, $_.FullName.Substring($Dir.Length + 1))
    }
