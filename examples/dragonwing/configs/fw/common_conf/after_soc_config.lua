-- This is executed after the SOC config and before the BSP config

if GET("platform.disable_hexagon") == true then
  disable_all_DSPs()
elseif GET("platform.hexagon_semihosting") == true then
  set_hexagon_property("enable_semihosting", true)
end
