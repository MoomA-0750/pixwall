# 設定画面の部品を、UI Automation で名前から探して動かす
#   settings-ui.ps1 -Fit fill / -Background custom（auto か custom） / -Toggle 一時停止 / -Speed 2 / -Position 25（0〜100） / -Frame 5（0 から） / -Button 表示を更新
param([string]$Fit, [string]$Background, [string]$Toggle, [double]$Speed = [double]::NaN, [double]$Position = [double]::NaN, [int]$Frame = -1, [string]$Button)
[Console]::OutputEncoding = [Text.Encoding]::UTF8
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$A = [System.Windows.Automation.AutomationElement]
$root = $A::RootElement.FindFirst('Children', (New-Object System.Windows.Automation.PropertyCondition($A::NameProperty, 'pixwall の設定')))
if (-not $root) { 'no window'; exit 1 }
# 同じ名前の文字（TextBlock）と見分けるため、パターンを持つものだけ探す
function Find($name, $pattern) {
    $c = New-Object System.Windows.Automation.AndCondition(
        (New-Object System.Windows.Automation.PropertyCondition($A::NameProperty, $name)),
        (New-Object System.Windows.Automation.PropertyCondition($pattern, $true)))
    $root.FindFirst('Descendants', $c)
}
# 選ぶ欄（ComboBox）の項目を名前で選ぶ
function Choose($boxName, $itemName) {
    $box = Find $boxName $A::IsExpandCollapsePatternAvailableProperty
    $box.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern).Expand()
    Start-Sleep -Milliseconds 500
    $item = $box.FindFirst('Descendants', (New-Object System.Windows.Automation.PropertyCondition($A::NameProperty, $itemName)))
    $item.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
    Start-Sleep -Milliseconds 300
    try { $box.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern).Collapse() } catch {}
}
if ($Fit) { Choose '並べ方' (@{ integer = '整数倍で最大'; fit = '画面に合わせる'; fill = '画面を埋める' })[$Fit] }
if ($Background) { Choose '余白の色' (@{ auto = '自動'; custom = '自分で選ぶ' })[$Background] }
if ($Toggle) { (Find $Toggle $A::IsTogglePatternAvailableProperty).GetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern).Toggle() }
if (-not [double]::IsNaN($Speed)) { (Find '速さ' $A::IsRangeValuePatternAvailableProperty).GetCurrentPattern([System.Windows.Automation.RangeValuePattern]::Pattern).SetValue([Math]::Log($Speed, 2)) }
if (-not [double]::IsNaN($Position)) { (Find '位置' $A::IsRangeValuePatternAvailableProperty).GetCurrentPattern([System.Windows.Automation.RangeValuePattern]::Pattern).SetValue($Position) }
if ($Frame -ge 0) { (Find '書き出す絵' $A::IsRangeValuePatternAvailableProperty).GetCurrentPattern([System.Windows.Automation.RangeValuePattern]::Pattern).SetValue($Frame) }
if ($Button) { (Find $Button $A::IsInvokePatternAvailableProperty).GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke() }
Start-Sleep -Milliseconds 500
Get-Content $env:APPDATA\pixwall\config.json
