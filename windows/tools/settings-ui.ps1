# 設定画面の部品を、UI Automation で名前から探して動かす
#   settings-ui.ps1 -Fit fill / -Toggle 一時停止 / -Speed 2 / -Button 表示を更新
param([string]$Fit, [string]$Toggle, [double]$Speed = [double]::NaN, [string]$Button)
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
if ($Fit) {
    $names = @{ integer = '整数倍で最大'; fit = '画面に合わせる'; fill = '画面を埋める' }
    $box = Find '並べ方' $A::IsExpandCollapsePatternAvailableProperty
    $box.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern).Expand()
    Start-Sleep -Milliseconds 500
    $item = $box.FindFirst('Descendants', (New-Object System.Windows.Automation.PropertyCondition($A::NameProperty, $names[$Fit])))
    $item.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
    Start-Sleep -Milliseconds 300
    try { $box.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern).Collapse() } catch {}
}
if ($Toggle) { (Find $Toggle $A::IsTogglePatternAvailableProperty).GetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern).Toggle() }
if (-not [double]::IsNaN($Speed)) { (Find '速さ' $A::IsRangeValuePatternAvailableProperty).GetCurrentPattern([System.Windows.Automation.RangeValuePattern]::Pattern).SetValue([Math]::Log($Speed, 2)) }
if ($Button) { (Find $Button $A::IsInvokePatternAvailableProperty).GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke() }
Start-Sleep -Milliseconds 500
Get-Content $env:APPDATA\pixwall\config.json
