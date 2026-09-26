# Lista nome e CRC32 de cada arquivo dentro dos zips de uma pasta, lendo so o
# diretorio central (fim do zip), sem descompactar. Para usar no Windows com o
# pendrive da Pandora; no Linux, o cruzar-roms.py le a pasta de ROMs direto.
#
# Uso: powershell -ExecutionPolicy Bypass -File listar-zips.ps1 -Dir H:\roms -Out zips.tsv
# Saida (TSV): zip<TAB>arquivo<TAB>crc<TAB>tamanho
param([string]$Dir, [string]$Out)

$sb = New-Object System.Text.StringBuilder
foreach ($z in Get-ChildItem -LiteralPath $Dir -Filter *.zip) {
  try {
    $fs = [System.IO.File]::OpenRead($z.FullName)
    $len = $fs.Length
    $tail = [Math]::Min($len, 70000)
    $buf = New-Object byte[] $tail
    $fs.Seek($len - $tail, 'Begin') | Out-Null
    [void]$fs.Read($buf, 0, $tail)
    $eocd = -1
    for ($i = $tail - 22; $i -ge 0; $i--) {
      if ($buf[$i] -eq 0x50 -and $buf[$i+1] -eq 0x4b -and $buf[$i+2] -eq 5 -and $buf[$i+3] -eq 6) { $eocd = $i; break }
    }
    if ($eocd -lt 0) { [void]$sb.AppendLine("$($z.Name)`t!sem-eocd`t0`t0"); $fs.Close(); continue }
    $cdSize = [BitConverter]::ToUInt32($buf, $eocd + 12)
    $cdOff  = [BitConverter]::ToUInt32($buf, $eocd + 16)
    $cd = New-Object byte[] $cdSize
    $fs.Seek($cdOff, 'Begin') | Out-Null
    [void]$fs.Read($cd, 0, $cdSize)
    $fs.Close()
    $p = 0
    while ($p + 46 -le $cdSize -and [BitConverter]::ToUInt32($cd, $p) -eq 0x02014b50) {
      $crc  = [BitConverter]::ToUInt32($cd, $p + 16)
      $size = [BitConverter]::ToUInt32($cd, $p + 24)
      $nl   = [BitConverter]::ToUInt16($cd, $p + 28)
      $el   = [BitConverter]::ToUInt16($cd, $p + 30)
      $cl   = [BitConverter]::ToUInt16($cd, $p + 32)
      $name = [System.Text.Encoding]::UTF8.GetString($cd, $p + 46, $nl)
      [void]$sb.AppendLine("$($z.Name)`t$name`t$('{0:x8}' -f $crc)`t$size")
      $p += 46 + $nl + $el + $cl
    }
  } catch {
    [void]$sb.AppendLine("$($z.Name)`t!erro:$($_.Exception.Message)`t0`t0")
  }
}
[System.IO.File]::WriteAllText($Out, $sb.ToString())
