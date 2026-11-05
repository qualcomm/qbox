# Generic Lua Model

## Introduction
The Qbox generic_lua_model is a systemc TLM model that can be described, instantiated and configured entirely from conf.lua to model very simple operations of a HW device without the need to write any C++ code. It introduces a TLM target socket interface that can be configured with specific address and size to access the conf.lua model defined registers on a specific address range, and a TLM initiator socket which can be connected for example to a router target socket and used to write to any memory location of any device model connected as a target to the router. The user can also define named target signals and initiator signals. 
The generic_lua_model is event driven. Event sources are register callbacks (`on_pre_read`, `on_pre_write`, `on_post_read`, `on_post_write`), target-signal callbacks, or actions scheduled from the start of simulation. Actions can write data to memory or write `true`/`false` to initiator signals. An action's optional `after_ns` value delays it; when omitted, the delay is 0 ns.
The model can stub or mock a device when software needs only a small set of register or signal behaviors. This may be enough for some software or OS boot flows, but does not guarantee a successful boot or complete device behavior.
It can be used as a fault injector as well since specific data can be written to specific memory locations or certain signals can be written to trigger certain actions after after_ns nanoseconds from the beginning of simulation. 


## How it works…
The semantics of the CCI parameters of the generic_lua_model is best described with an example:

```lua
clk_ctl = {
      moduletype = "generic_lua_model";
      args = {"&gen_top","&backup_reg_memory"};
      initiator_socket = {bind = "&router.target_socket"};
      target_socket = {address=0x100000, size=0x200000, bind = "&router.initiator_socket"};
      {after_ns = 100, address = 0x19984000, data=0xFFFFFFFF};
      {after_ns = 400, name = "output_sig0", output = false};
      registers = {
            { name = "reg0", address = 0x0, reg_mask = 0xf000000f, default = 0xffffffff, 
              on_pre_write = {
                  {
                    value = 0x0;
                    mask = 0x0ffffff0;
                  };
                };
                on_post_write = {
                  {
                    address = 0x80000200;
                    data = 0xababcdcd;
                    after_ns = 20;
                  };
                  {
                        name = "output_sig1";
                        output = true;
                        after_ns = 10;
                  };
                };
            };
      };
      signals = {
            { name = "target_sig_0";
                on_true = {
                    {
                        address = 0x80000000;
                        data = 0xdeadbeef;
                        after_ns = 1;
                    };
                    {
                        name = "output_sig1";
                        output = true;
                    };
                };
                on_false = {
                    {
                        address = 0x80000000;
                        data = 0xbeefdead;
                    };
                    {
                        name = "output_sig1";
                        output = false;
                    };
                };
            };
        };
    };
```
- moduletype: determines the type of the model instantiated in conf.lua, in this case it should be “generic_lua_model”.


- args: specify the dependencies for the C++ constructor. In this example, the first argument refers to a container with a child named `reg_router`; the model looks up that child and uses it as a `gs::reg_router<>`. The second argument is the `gs::gs_memory<>` used for register storage. The other constructor takes a `gs::reg_router<>*` directly.


- initiator_socket: this is a TLM initiator socket which can be connected to the router target socket to access any memory location in the address range of all models' TLM target sockets connected to the router.


- target_socket: this is a TLM target socket which can be connected to a router initiator socket to be accessed by any bus master on the address range configured by the address and size CCI parameters here: {address=0x100000, size=0x200000, bind = "&router.initiator_socket"}.


- after_ns table entries: in the above example: {after_ns = 100, address = 0x19984000, data=0xFFFFFFFF} means that this is a time triggered action executed after 100 nanoseconds from the beginning of the simulation and the action will be: write to address 0x19984000 this uint32_t data 0xFFFFFFFF. 
The second example: {after_ns = 400, name = "output_sig0", output = false} means: instantiate a new initiator signal named: output_sig0 and after 400 nanoseconds from the beginning of the simulation, write value false to this signal, so the user can connect this signal to a target signal of any model and false will be written on this target signal connected to output_sig0 after 400 nanoseconds from the beginning of the simulation.


- registers: this table entry describes a set of registers combined with actions needed to be taken on each register, each register must have unique name, unique address and at least one action: {on_pre_read, on_pre_write, on_post_read and on_post_write}.
The register can also have optional parameters: 
default to assign a default value to the register.
reg_mask sets the mask applied to writes to this register. For example, 0xf000000f allows writes to the first 4 bits and the last 4 bits; writes to bits 4 through 27 do not change those bits. The default reg_mask is 0xFFFFFFFF.
For register callback actions, optional `if_val` makes the
action conditional: `on_pre_read` compares it with the current
register value; the other callback types compare it with the
transaction value. The optional `mask` and `value` fields
control the register-value update.
The actions that can be taken on one of  {on_pre_read, on_pre_write, on_post_read and on_post_write} action sources can be:
1. mutate the register itself according to this procedure: 
first set the mask of the register to be 0xFFFFFFFF to allow the mutation of all the 32 bits of the register, then apply this equation: 
new_register_value = (current_register_value & (~mask)) | ( value & mask); then set the mask again to the reg_mask CCI parameter. This means that in the above example, on pre_write (which means the callback function that will be executed before the actual value in the coming transaction is being written to the physical register memory) if the bus master has written the value 0x12345678 to the register, before this value is being written and the reg_mask is applied, the actions mentioned in the lua description will be executed first, so the current value in the register memory now is 0xFFFFFFFF (the default value) and the above equation will be executed because we have {value=0x0; mask = 0x0ffffff0;} so the newly stored value in the register memory will be: (0xffffffff & ~(0x0ffffff0)) | (0x0 & 0x0ffffff0) = 0xf000000f then the transaction data coming from the bus master 0x12345678 will be now written but masked by the reg_mask: 0xf000000f and this means that the first 4 bits and last 4 bits are only allowed to change so now the final value written to the physical register memory location will be 0x10000008.
2. write data to certain memory location and optionally after_ns nanoseconds, as shown in the above example on the on_post_write action so data: 0xababcdcd will be written to address:  0x80000200 after 20 nanoseconds from the time the register was written to by the initiator socket connected to its target socket.
3. write true or false value to an initiator signal, and optionally schedule the signal write to be after_ns nanoseconds, as shown in the above example on the on_post_write action, there is an initiator signal called output_sig1 and it will be written with value true after 10 nanoseconds from the time the register was written to by the initiator socket connected to its target socket.


- signals: this table entry describes the named target signals that can be instantiated with a given unique name and connected to initiator signals of the other models in the platform. In the above example a target signal named: target_sig_0 will be instantiated and it can have one of 2 actions {on_true, on_false}, any one of these 2 actions or both of them can be used. The associated behaviour to on_true and/or on_false can be number 2 and 3 of the registers described in the above point.
