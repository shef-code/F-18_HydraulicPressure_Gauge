# Renders the "bitmap" gauge style: realistic face + needle assets -> PNG previews + RGB565 headers.
# Run:  powershell -ExecutionPolicy Bypass -File tools\render_bitmap_assets.ps1
Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = 'Stop'
$root   = Split-Path -Parent $PSScriptRoot
$outHdr = Join-Path $root 'assets_bitmap'
$outPng = Join-Path $root 'docs\preview'
New-Item -ItemType Directory -Force $outHdr, $outPng | Out-Null
$S = 4                                  # supersample factor for the face
$FONT = 'Bahnschrift SemiBold'

# ---- geometry shared with the sketch (angles: degrees clockwise from 12 o'clock) ----
$A0 = 80.0; $DEG_PER_K = 63.0           # 0 psi at 80deg, 63deg per 1000 psi
$BAND0 = 240.0; $BAND1 = 287.0          # normal-operating band

function C([double]$r,[double]$g,[double]$b,[double]$a=255){
  [System.Drawing.Color]::FromArgb([int][math]::Min(255,[math]::Max(0,$a)),[int][math]::Min(255,[math]::Max(0,$r)),[int][math]::Min(255,[math]::Max(0,$g)),[int][math]::Min(255,[math]::Max(0,$b)))
}
function NewG($bmp){ $g=[System.Drawing.Graphics]::FromImage($bmp); $g.SmoothingMode='AntiAlias'; $g.TextRenderingHint='AntiAliasGridFit'; $g.PixelOffsetMode='HighQuality'; $g }
function Pol($cx,$cy,$r,$th){ $t=$th*[math]::PI/180; [System.Drawing.PointF]::new([single]($cx+$r*[math]::Sin($t)),[single]($cy-$r*[math]::Cos($t))) }
function Br($c){ New-Object System.Drawing.SolidBrush $c }
function Circ($g,$brush,$cx,$cy,$r){ $g.FillEllipse($brush,[single]($cx-$r),[single]($cy-$r),[single](2*$r),[single](2*$r)) }

# Anisotropic metal disc (light from lightTh) drawn as thin pie slices; centre is covered afterwards
function MetalDisc($g,$cx,$cy,$rOut,$lightTh,$lo,$hi,$tint){
  $step=0.5
  for($a=0.0;$a -lt 360;$a+=$step){
    $d=[math]::Cos(($a-$lightTh)*[math]::PI/180)
    $v=[math]::Pow(0.5+0.5*$d,1.6)
    $spec=[math]::Pow([math]::Max(0,$d),24)*0.35
    $k=$lo+($hi-$lo)*$v+255*$spec
    $br=Br (C ($k*$tint[0]) ($k*$tint[1]) ($k*$tint[2]))
    $g.FillPie($br,[single]($cx-$rOut),[single]($cy-$rOut),[single](2*$rOut),[single](2*$rOut),[single]($a-90-0.02),[single]($step+0.05))
    $br.Dispose()
  }
}

# MS33558 (MIL-STD aircraft instrument dial lettering), installed per-user -> load the file directly.
# Its em height equals its cap height, so the em sizes below are cap heights in dial pixels.
$FONTFILE = Join-Path $env:LOCALAPPDATA 'Microsoft\Windows\Fonts\MS33558.ttf'
$pfc = New-Object System.Drawing.Text.PrivateFontCollection; $pfc.AddFontFile($FONTFILE)
$FAM  = $pfc.Families[0]
$FMT  = [System.Drawing.StringFormat]::GenericTypographic
$CAPR = 0.0   # cap-height / em, measured once
function CapRatio(){
  if($script:CAPR -eq 0.0){
    $p=New-Object System.Drawing.Drawing2D.GraphicsPath; $p.AddString('H',$FAM,0,[single]100,[System.Drawing.PointF]::new(0,0),$FMT)
    $script:CAPR=$p.GetBounds().Height/100.0
  }
  $script:CAPR
}
# Draws text centred on (cx, cy) where cy is the vertical centre of the cap height; em in the same units as cx/cy
function CapText($g,$txt,$em,$cx,$cy,$brush,$track=0){
  # per-glyph layout matching the procedural atlas: rounded advances (+tracking), tightened word space
  $mb=New-Object System.Drawing.Bitmap 4,4; $mg=[System.Drawing.Graphics]::FromImage($mb)
  $f=New-Object System.Drawing.Font $FAM,([single]$em),([System.Drawing.FontStyle]::Regular),([System.Drawing.GraphicsUnit]::Pixel)
  $px=$script:S
  $advs=@(); $total=0.0
  foreach($ch in $txt.ToCharArray()){
    if($ch -eq ' '){ $a=[math]::Round($em/$px*0.4)+$track } else { $a=[math]::Round($mg.MeasureString([string]$ch,$f,10000,$FMT).Width/$px)+$track }
    $advs+=$a; $total+=$a
  }
  $asc=$FAM.GetCellAscent(0)/$FAM.GetEmHeight(0)*$em; $cap=(CapRatio)*$em
  $x=$cx-$total*$px/2.0; $i=0
  foreach($ch in $txt.ToCharArray()){
    if($ch -ne ' '){
      $p=New-Object System.Drawing.Drawing2D.GraphicsPath; $p.AddString([string]$ch,$FAM,0,[single]$em,[System.Drawing.PointF]::new(0,0),$FMT)
      $m=New-Object System.Drawing.Drawing2D.Matrix; $m.Translate([single]$x,[single]($cy+$cap/2-$asc)); $p.Transform($m)
      $g.FillPath($brush,$p)
    }
    $x+=$advs[$i]*$px; $i++
  }
  $mg.Dispose(); $mb.Dispose()
}

function RenderFace([bool]$night){
  $N=240*$S; $bmp=New-Object System.Drawing.Bitmap $N,$N
  $g=NewG $bmp; $g.Clear((C 0 0 0))
  $cx=$N/2.0; $cy=$N/2.0; $u=$S
  if($night){ $tint=@(0.55,0.62,0.55); $mark=(C 90 255 100); $dialC=(C 10 16 10); $dialE=(C 3 6 3); $hubLo=16; $hubHi=90; $gl=0.35 }
  else      { $tint=@(1.0,1.0,1.02);   $mark=(C 238 234 222); $dialC=(C 34 34 36); $dialE=(C 8 8 9);  $hubLo=60; $hubHi=200; $gl=1.0 }

  # matte dial to the edge of the display, hot-spot up-left, vignette toward the rim
  $path=New-Object System.Drawing.Drawing2D.GraphicsPath; $path.AddEllipse([single]($cx-120*$u),[single]($cy-120*$u),[single](240*$u),[single](240*$u))
  $pg=New-Object System.Drawing.Drawing2D.PathGradientBrush $path
  $pg.CenterPoint=[System.Drawing.PointF]::new([single]($cx-22*$u),[single]($cy-26*$u)); $pg.CenterColor=$dialC; $pg.SurroundColors=@($dialE)
  $g.FillPath($pg,$path)
  for($i=0;$i -lt 14;$i++){
    $r=(106.5+$i)*$u; $al=255*0.45*(($i+0.5)/14.0)
    $p=New-Object System.Drawing.Pen (C 0 0 0 $al),([single](1.0*$u)); $g.DrawEllipse($p,[single]($cx-$r),[single]($cy-$r),[single](2*$r),[single](2*$r)); $p.Dispose()
  }

  # normal-operating band (r 91 .. 108.5)
  $bandR=99.75*$u
  $bp=New-Object System.Drawing.Pen $mark,([single](17.5*$u)); $bp.StartCap='Flat'; $bp.EndCap='Flat'
  $g.DrawArc($bp,[single]($cx-$bandR),[single]($cy-$bandR),[single](2*$bandR),[single](2*$bandR),[single]($BAND0-90),[single]($BAND1-$BAND0))

  # ticks: every 200 psi (12.6 deg), major each 1000 psi
  for($i=0;$i -le 25;$i++){
    $th=$A0+$i*12.6; $major=(($i % 5) -eq 0)
    if($major){ $r0=86; $w=3.5 } else { $r0=98; $w=2.0 }
    $pt=New-Object System.Drawing.Pen $mark,([single]($w*$u)); $pt.StartCap='Flat'; $pt.EndCap='Flat'
    $g.DrawLine($pt,(Pol $cx $cy ($r0*$u) $th),(Pol $cx $cy (109*$u) $th)); $pt.Dispose()
  }
  # numerals 0..5 and legends (sizes follow the original artwork)
  $tb=Br $mark
  for($k=0;$k -le 5;$k++){ $p=Pol $cx $cy (74*$u) ($A0+$k*$DEG_PER_K); $nx=$p.X; $ny=$p.Y; if($k -eq 1){ $nx=163*$u; $ny=191*$u }; if($k -eq 2){ $nx=96*$u; $ny=190*$u }; CapText $g "$k" (20*$u) $nx $ny $tb }
  CapText $g 'HYD PRESS'  (22*$u) (121*$u) (81.5*$u)  $tb 1
  CapText $g 'PSI X 1000' (16*$u) (128*$u) (164.5*$u) $tb 1

  # centre hub: thin machined ring around a black well
  MetalDisc $g $cx $cy (15.6*$u) 315 $hubLo $hubHi $tint
  Circ $g (Br (C 4 4 4)) $cx $cy (13.6*$u)

  # glass: crescent reflection + soft streak
  if($night){ $aMul=0.35 } else { $aMul=1.0 }
  $cres=New-Object System.Drawing.Drawing2D.GraphicsPath; $cres.AddEllipse([single]($cx-112*$u),[single]($cy-112*$u),[single](224*$u),[single](224*$u))
  $cut=New-Object System.Drawing.Drawing2D.GraphicsPath;  $cut.AddEllipse([single]($cx-112*$u+18*$u),[single]($cy-112*$u+22*$u),[single](224*$u),[single](224*$u))
  $reg=New-Object System.Drawing.Region $cres; $reg.Exclude($cut)
  $lg=New-Object System.Drawing.Drawing2D.LinearGradientBrush ([System.Drawing.PointF]::new([single]($cx-100*$u),[single]($cy-100*$u))),([System.Drawing.PointF]::new([single]$cx,[single]$cy)),(C 255 255 255 (44*$aMul)),(C 255 255 255 0)
  $g.FillRegion($lg,$reg)
  $streak=New-Object System.Drawing.Drawing2D.GraphicsPath
  $streak.AddPolygon(@([System.Drawing.PointF]::new([single]$cx,[single]$cy),[System.Drawing.PointF]::new([single]($cx+44*$u),[single]($cy-120*$u)),[System.Drawing.PointF]::new([single]($cx+88*$u),[single]($cy-82*$u))))
  $sg=New-Object System.Drawing.Drawing2D.LinearGradientBrush ([System.Drawing.PointF]::new([single]($cx+66*$u),[single]($cy-100*$u))),([System.Drawing.PointF]::new([single]($cx+11*$u),[single]($cy-22*$u))),(C 255 255 255 (12*$aMul)),(C 255 255 255 0)
  $g.FillPath($sg,$streak)
  $g.Dispose()

  # downsample to 240x240
  $out=New-Object System.Drawing.Bitmap 240,240
  $g2=[System.Drawing.Graphics]::FromImage($out); $g2.InterpolationMode='HighQualityBicubic'; $g2.PixelOffsetMode='HighQuality'
  $g2.DrawImage($bmp,[System.Drawing.Rectangle]::new(0,0,240,240),0,0,$N,$N,[System.Drawing.GraphicsUnit]::Pixel); $g2.Dispose(); $bmp.Dispose()
  $out
}

# ---- needle (33x120, pivot 16,103): 19 px wide, tip 103 px from the pivot, numbered dark tag below the pointer ----
function NeedleGeom($g,[int]$num,[bool]$night,[bool]$mask,$u){
  $cx=16.5*$u; $py=103.5*$u
  $fx={ param([double]$v) [single]($cx+$v*$u) }
  $fy={ param([double]$v) [single]($py+$v*$u) }
  $fp={ param([double]$x,[double]$y) [System.Drawing.PointF]::new((& $fx $x),(& $fy $y)) }
  if($mask){ $ptrB=Br (C 255 255 255); $bodyB=$ptrB; $edgeC=$null }
  else{
    if($night){ $c1=(C 40 255 70);  $c2=(C 10 150 30);  $boxA=(C 0 28 4);   $boxB=(C 0 12 2);   $txt=(C 120 255 140); $edgeC=(C 40 220 60) }
    else      { $c1=(C 255 255 252); $c2=(C 168 168 164); $boxA=(C 52 52 54); $boxB=(C 18 18 19); $txt=(C 240 240 232); $edgeC=(C 120 120 118) }
    $ptrB=New-Object System.Drawing.Drawing2D.LinearGradientBrush ([System.Drawing.PointF]::new((& $fx -9.5),0)),([System.Drawing.PointF]::new((& $fx 9.5),0)),$c2,$c2
    $bl=New-Object System.Drawing.Drawing2D.ColorBlend 3; $bl.Colors=@($c2,$c1,$c2); $bl.Positions=@(0.0,0.5,1.0); $ptrB.InterpolationColors=$bl
    $bodyB=New-Object System.Drawing.Drawing2D.LinearGradientBrush ([System.Drawing.PointF]::new((& $fx -9.5),0)),([System.Drawing.PointF]::new((& $fx 9.5),0)),$boxA,$boxA
    $b2=New-Object System.Drawing.Drawing2D.ColorBlend 3; $b2.Colors=@($boxB,$boxA,$boxB); $b2.Positions=@(0.0,0.5,1.0); $bodyB.InterpolationColors=$b2
  }
  # tag (y -49 .. -15.5)
  $g.FillRectangle($bodyB,(& $fx -9.5),(& $fy -49),[single](19*$u),[single](33.5*$u))
  if(-not $mask){ $ep=New-Object System.Drawing.Pen $edgeC,([single](0.7*$u)); $g.DrawLine($ep,(& $fp -9.5 -49),(& $fp -9.5 -15.5)); $g.DrawLine($ep,(& $fp 9.5 -49),(& $fp 9.5 -15.5)) }
  # pointer (y -103.5 .. -49)
  $g.FillPolygon($ptrB,@((& $fp -0.5 -103.5),(& $fp 0.5 -103.5),(& $fp 9.5 -81),(& $fp 9.5 -49),(& $fp -9.5 -49),(& $fp -9.5 -81)))
  if(-not $mask){ CapText $g "$num" (20*$u) (& $fx 0) (& $fy -32) (Br $txt) }
}

function RenderNeedle([int]$num,[bool]$night){
  $S2=8; $W=33*$S2; $H=120*$S2
  $col=New-Object System.Drawing.Bitmap $W,$H; $gc=NewG $col; $gc.Clear((C 10 10 11)); NeedleGeom $gc $num $night $false $S2; $gc.Dispose()
  $msk=New-Object System.Drawing.Bitmap $W,$H; $gm=NewG $msk; $gm.Clear((C 0 0 0));     NeedleGeom $gm $num $night $true  $S2; $gm.Dispose()
  $oc=New-Object System.Drawing.Bitmap 33,120; $om=New-Object System.Drawing.Bitmap 33,120
  foreach($pair in @(@($col,$oc),@($msk,$om))){
    $g2=[System.Drawing.Graphics]::FromImage($pair[1]); $g2.InterpolationMode='HighQualityBicubic'; $g2.PixelOffsetMode='HighQuality'
    $g2.DrawImage($pair[0],[System.Drawing.Rectangle]::new(0,0,33,120),0,0,$W,$H,[System.Drawing.GraphicsUnit]::Pixel); $g2.Dispose(); $pair[0].Dispose()
  }
  ,@($oc,$om)
}

# ---- RGB565 export with 4x4 ordered dither (hides banding in the gradients) ----
$bayer=@(0,8,2,10, 12,4,14,6, 3,11,1,9, 15,7,13,5)
function To565($r,$g,$b,$x,$y){
  $d=$bayer[($y -band 3)*4+($x -band 3)]-7.5
  $rr=[int][math]::Min(255,[math]::Max(0,$r+$d*0.5)); $gg=[int][math]::Min(255,[math]::Max(0,$g+$d*0.25)); $bb=[int][math]::Min(255,[math]::Max(0,$b+$d*0.5))
  ((($rr -shr 3) -shl 11) -bor (($gg -shr 2) -shl 5) -bor ($bb -shr 3))
}
function WriteHeader($path,$name,$comment,$vals,$w){
  $sb=New-Object System.Text.StringBuilder
  [void]$sb.AppendLine("// $comment")
  [void]$sb.AppendLine('// RGB565, use with setSwapBytes(true). Generated by tools/render_bitmap_assets.ps1 - do not edit by hand.')
  [void]$sb.AppendLine('#pragma once'); [void]$sb.AppendLine('#include <Arduino.h>'); [void]$sb.AppendLine("const uint16_t $name [] = {")
  for($i=0;$i -lt $vals.Count;$i+=$w){
    $end=[math]::Min($i+$w,$vals.Count)-1
    [void]$sb.AppendLine((($vals[$i..$end] | ForEach-Object { '0x{0:x4}' -f $_ }) -join ', ')+',')
  }
  [void]$sb.AppendLine('};'); [IO.File]::WriteAllText($path,$sb.ToString())
}
function FaceToVals($bmp){
  $v=New-Object 'System.Collections.Generic.List[int]'
  for($y=0;$y -lt 240;$y++){ for($x=0;$x -lt 240;$x++){
    $dx=$x+0.5-120; $dy=$y+0.5-120
    if(($dx*$dx+$dy*$dy) -gt 14400){ $v.Add(0) } else { $c=$bmp.GetPixel($x,$y); $v.Add((To565 $c.R $c.G $c.B $x $y)) } } }
  ,$v
}
function NeedleToVals($pair){
  $v=New-Object 'System.Collections.Generic.List[int]'
  for($y=0;$y -lt 120;$y++){ for($x=0;$x -lt 33;$x++){
    $m=$pair[1].GetPixel($x,$y).R
    if($m -lt 90){ $v.Add(0x0120) } else { $c=$pair[0].GetPixel($x,$y); $q=To565 $c.R $c.G $c.B $x $y; if($q -eq 0x0120){ $q=0x0121 }; $v.Add($q) } } }
  ,$v
}

foreach($night in @($false,$true)){
  if($night){ $tag='Green' } else { $tag='Day' }
  Write-Host "Rendering $tag face..."
  $face=RenderFace $night
  $face.Save((Join-Path $outPng "bitmap_face$tag.png"),[System.Drawing.Imaging.ImageFormat]::Png)
  WriteHeader (Join-Path $outHdr "face$tag.h") "bmpFace$tag" "Realistic hydraulic pressure dial, 240x240 ($tag)" (FaceToVals $face) 24
  $face.Dispose()
  foreach($n in 1,2){
    Write-Host "Rendering needle $n $tag..."
    $pair=RenderNeedle $n $night
    $pv=New-Object System.Drawing.Bitmap 33,120
    for($y=0;$y -lt 120;$y++){ for($x=0;$x -lt 33;$x++){ if($pair[1].GetPixel($x,$y).R -lt 90){ $pv.SetPixel($x,$y,(C 90 40 120)) } else { $pv.SetPixel($x,$y,$pair[0].GetPixel($x,$y)) } } }
    $big=New-Object System.Drawing.Bitmap 132,480; $gb=[System.Drawing.Graphics]::FromImage($big); $gb.InterpolationMode='NearestNeighbor'; $gb.PixelOffsetMode='Half'; $gb.DrawImage($pv,0,0,132,480); $gb.Dispose()
    $big.Save((Join-Path $outPng "bitmap_needle$n$tag.png"),[System.Drawing.Imaging.ImageFormat]::Png); $big.Dispose(); $pv.Dispose()
    WriteHeader (Join-Path $outHdr "needle$n$tag.h") "bmpNeedle$n$tag" "Needle $n ($tag), 33x120, pivot (16,103), transparent=0x0120" (NeedleToVals $pair) 33
    $pair[0].Dispose(); $pair[1].Dispose()
  }
}
Write-Host 'done'
