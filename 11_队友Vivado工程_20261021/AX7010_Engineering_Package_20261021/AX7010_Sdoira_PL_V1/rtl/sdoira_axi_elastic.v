`timescale 1ns/1ps
// Compatibility front end for the reference arbiter's always-ready assumption.
// The legacy arbiter has at most one outstanding transaction and waits for B
// or the last R beat before issuing another. Buffer one complete 256-beat
// write burst, plus independent AW/AR address slots. Never depend on external
// READY before presenting VALID; hold every payload stable until handshake.
module sdoira_axi_elastic(
 input clk,input rst_n,
 input [31:0] s_awaddr,input [7:0] s_awlen,input s_awvalid,output s_awready,
 input [31:0] s_wdata,input [3:0] s_wstrb,input s_wlast,input s_wvalid,output s_wready,
 input [31:0] s_araddr,input [7:0] s_arlen,input s_arvalid,output s_arready,
 output reg [31:0] m_awaddr,output reg [7:0] m_awlen,output reg m_awvalid,input m_awready,
 output [31:0] m_wdata,output [3:0] m_wstrb,output m_wlast,output m_wvalid,input m_wready,
 output reg [31:0] m_araddr,output reg [7:0] m_arlen,output reg m_arvalid,input m_arready,
 output [31:0] debug_status);
(* ram_style="distributed" *) reg [36:0] wfifo[0:255];
reg [7:0] head,tail;
reg [8:0] count;
wire push=s_wvalid&&s_wready;
wire pop=m_wvalid&&m_wready;
assign s_awready=!m_awvalid;
assign s_arready=!m_arvalid;
assign s_wready=count<256;
assign m_wvalid=count!=0;
assign {m_wlast,m_wstrb,m_wdata}=wfifo[head];
assign debug_status={17'b0,m_arready,m_wready,m_awready,m_arvalid,m_awvalid,m_wvalid,count};
always @(posedge clk)begin
 if(!rst_n)begin
  m_awaddr<=0;m_awlen<=0;m_awvalid<=0;
  m_araddr<=0;m_arlen<=0;m_arvalid<=0;
  head<=0;tail<=0;count<=0;
 end else begin
  if(m_awvalid&&m_awready)m_awvalid<=0;
  if(s_awvalid&&s_awready)begin m_awaddr<=s_awaddr;m_awlen<=s_awlen;m_awvalid<=1;end
  if(m_arvalid&&m_arready)m_arvalid<=0;
  if(s_arvalid&&s_arready)begin m_araddr<=s_araddr;m_arlen<=s_arlen;m_arvalid<=1;end
  if(push)begin wfifo[tail]<={s_wlast,s_wstrb,s_wdata};tail<=tail+1'b1;end
  if(pop)head<=head+1'b1;
  case({push,pop})
   2'b10:count<=count+1'b1;
   2'b01:count<=count-1'b1;
   default:count<=count;
  endcase
 end
end
endmodule
