// Adapted from sdoira DVP wiring; original project GPL-3.0-or-later.

`timescale 1 ns / 1 ps

//`define USE_DUMMY_RECT
//`define USE_DUMMY_XSBL2
//`define USE_DUMMY_BM

module sdoira_bm_axi # (
	// Parameters of Axi Slave Bus Interface S00_AXI
	parameter integer C_S_AXI_DATA_WIDTH = 32,
	parameter integer C_S_AXI_ADDR_WIDTH = 14,
	
	// Parameters of Axi Master Bus Interface M00_AXI
	parameter  C_M00_AXI_TARGET_SLAVE_BASE_ADDR = 32'h40000000,
	parameter integer C_M00_AXI_BURST_LEN    = 16,
	parameter integer C_M00_AXI_ID_WIDTH     =  1,
	parameter integer C_M00_AXI_ADDR_WIDTH   = 32,
	parameter integer C_M00_AXI_DATA_WIDTH   = 32,
	parameter integer C_M00_AXI_AWUSER_WIDTH =  0,
	parameter integer C_M00_AXI_ARUSER_WIDTH =  0,
	parameter integer C_M00_AXI_WUSER_WIDTH  =  0,
	parameter integer C_M00_AXI_RUSER_WIDTH  =  0,
	parameter integer C_M00_AXI_BUSER_WIDTH  =  0
) (
	// DVP Interface
	pclk1, vsync1, href1, d1,
	pclk2, vsync2, href2, d2,
	
	// I2C Interface
	scl1, sda1,
	scl2, sda2,
	
	// Debug
	led,
	intr,

	// AXI Interface
	s00_axi_aclk,
	s00_axi_aresetn,
	s00_axi_awaddr,
	s00_axi_awprot,
	s00_axi_awvalid,
	s00_axi_awready,
	s00_axi_wdata,
	s00_axi_wstrb,
	s00_axi_wvalid,
	s00_axi_wready,
	s00_axi_bresp,
	s00_axi_bvalid,
	s00_axi_bready,
	s00_axi_araddr,
	s00_axi_arprot,
	s00_axi_arvalid,
	s00_axi_arready,
	s00_axi_rdata,
	s00_axi_rresp,
	s00_axi_rvalid,
	s00_axi_rready,
	
	m00_axi_aclk,
	m00_axi_aresetn,
	m00_axi_awid,
	m00_axi_awaddr,
	m00_axi_awlen,
	m00_axi_awsize,
	m00_axi_awburst,
	m00_axi_awlock,
	m00_axi_awcache,
	m00_axi_awprot,
	m00_axi_awqos,
	m00_axi_awuser,
	m00_axi_awvalid,
	m00_axi_awready,
	m00_axi_wdata,
	m00_axi_wstrb,
	m00_axi_wlast,
	m00_axi_wuser,
	m00_axi_wvalid,
	m00_axi_wready,
	m00_axi_bid,
	m00_axi_bresp,
	m00_axi_buser,
	m00_axi_bvalid,
	m00_axi_bready,
	m00_axi_arid,
	m00_axi_araddr,
	m00_axi_arlen,
	m00_axi_arsize,
	m00_axi_arburst,
	m00_axi_arlock,
	m00_axi_arcache,
	m00_axi_arprot,
	m00_axi_arqos,
	m00_axi_aruser,
	m00_axi_arvalid,
	m00_axi_arready,
	m00_axi_rid,
	m00_axi_rdata,
	m00_axi_rresp,
	m00_axi_rlast,
	m00_axi_ruser,
	m00_axi_rvalid,
	m00_axi_rready
);


//==========================================================================
// Parameter Declaration
//==========================================================================
parameter VERSION = 32'h53444f31;


//==========================================================================
// Port Declaration
//==========================================================================
// DVP Interface
input			pclk1;
input			vsync1, href1;
input	[9:2]	d1;
input			pclk2;
input			vsync2, href2;
input	[9:2]	d2;

// I2C Interface
output			scl1;
inout			sda1;
output			scl2;
inout			sda2;

// Debug
output	[1:0]	led;
output			intr;

// AXI Interface
input			s00_axi_aclk;
input			s00_axi_aresetn;
input	[13:0]	s00_axi_awaddr;
input	[2:0]	s00_axi_awprot;
input			s00_axi_awvalid;
output			s00_axi_awready;
input	[31:0]	s00_axi_wdata;
input	[3:0]	s00_axi_wstrb;
input			s00_axi_wvalid;
output			s00_axi_wready;
output	[1:0]	s00_axi_bresp;
output			s00_axi_bvalid;
input			s00_axi_bready;
input	[13:0]	s00_axi_araddr;
input	[2:0]	s00_axi_arprot;
input			s00_axi_arvalid;
output			s00_axi_arready;
output	[31:0]	s00_axi_rdata;
output	[1:0]	s00_axi_rresp;
output			s00_axi_rvalid;
input			s00_axi_rready;

input			m00_axi_aclk;
input			m00_axi_aresetn;
output			m00_axi_awid;
output	[31:0]	m00_axi_awaddr;
output	[7:0]	m00_axi_awlen;
output	[2:0]	m00_axi_awsize;
output	[1:0]	m00_axi_awburst;
output			m00_axi_awlock;
output	[3:0]	m00_axi_awcache;
output	[2:0]	m00_axi_awprot;
output	[3:0]	m00_axi_awqos;
output			m00_axi_awuser;
output			m00_axi_awvalid;
input			m00_axi_awready;
output	[31:0]	m00_axi_wdata;
output	[3:0]	m00_axi_wstrb;
output			m00_axi_wlast;
output			m00_axi_wuser;
output			m00_axi_wvalid;
input			m00_axi_wready;
input			m00_axi_bid;
input	[1:0]	m00_axi_bresp;
input			m00_axi_buser;
input			m00_axi_bvalid;
output			m00_axi_bready;
output			m00_axi_arid;
output	[31:0]	m00_axi_araddr;
output	[7:0]	m00_axi_arlen;
output	[2:0]	m00_axi_arsize;
output	[1:0]	m00_axi_arburst;
output			m00_axi_arlock;
output	[3:0]	m00_axi_arcache;
output	[2:0]	m00_axi_arprot;
output	[3:0]	m00_axi_arqos;
output			m00_axi_aruser;
output			m00_axi_arvalid;
input			m00_axi_arready;
input			m00_axi_rid;
input	[31:0]	m00_axi_rdata;
input	[1:0]	m00_axi_rresp;
input			m00_axi_rlast;
input			m00_axi_ruser;
input			m00_axi_rvalid;
output			m00_axi_rready;


//==========================================================================
// Reg/Wire Declaration
//==========================================================================
// AXI Interface
wire			clk, rst_n;
//wire	[1:0]	led;
wire	[1:0]	ptn_sel;
wire	[11:0]	ibus_addr;
wire	[3:0]	ibus_cs;
wire			ibus_wr;
wire	[31:0]	ibus_wrdata;
wire			ibus_cs_i2c, ibus_cs_frm, ibus_cs_mes, ibus_cs_sel;
wire			ibus_cs_grb, ibus_cs_xsbl, ibus_cs_bm, ibus_cs_arb;
wire			ibus_cs_rect;
wire	[31:0]	ibus_rddata;
wire			intr;

// Image Sensor Interface
wire	[31:0]	ibus_rddata_sel;
wire			vsync_sel;
wire			href_sel;
wire	[7:0]	d1_sel, d2_sel;

wire			vsync_ptn, href_ptn;
wire	[7:0]	d1_ptn, d2_ptn;

// I2C Interface
wire	[31:0]	ibus_rddata_i2c;
wire			sda1_o, sda2_o;
wire			sda1_i, sda2_i;

// Frame Measurement
wire	[31:0]	ibus_rddata_mes;

// grb
wire	[31:0]	ibus_rddata_grb;
wire			grb_req;
wire	[31:0]	grb_dout;
wire			grb_vout;

// rect
wire		[3:0]	rect_fcnt;
wire	[31:0]	ibus_rddata_rect;
wire			rect_req;
wire	[31:0]	rect_dout;
wire	[3:0]	rect_strb;
wire			rect_vout;

// xsbl
wire			xsblr_vin;
wire	[3:0]	xsbl_fcnt;
wire	[31:0]	ibus_rddata_xsbl;
wire			xsblr_req;
wire			xsblr_vout;
wire	[31:0]	xsblr_dout;
wire			xsblw_req;
wire			xsblw_vout;
wire	[31:0]	xsblw_dout;
wire	[3:0]	xsblw_strb;

// bm
wire			drd_vin;
wire	[3:0]	bm_fcnt;
wire			bm_enb;
wire	[31:0]	ibus_rddata_bm;
wire			drd_req;
wire			drd_vout;
wire	[31:0]	drd_dout;
wire			dwr_req;
wire			dwr_vout;
wire	[31:0]	dwr_dout;

// arb
wire	[5:0]	arb_req;
wire			arb_vin;
wire	[31:0]	arb_din;
wire	[3:0]	arb_strb;
wire			xsblw_ack, rect_ack, dwr_ack, drd_ack, xsblr_ack, grb_ack;
wire	[31:0]	ibus_rddata_arb;
wire	[5:0]	arb_done;
wire	[5:0]	arb_ack;
wire			arb_vout;
wire	[31:0]	arb_dout;
wire			grb_done, rect_done, xsbl_done, bm_done;
wire	[31:0]	axi_awaddr;
wire	[7:0]	axi_awlen;
wire			axi_awvalid;
wire	[31:0]	axi_wdata;
wire			axi_wvalid;
wire			axi_wlast;
wire	[31:0]	axi_araddr;
wire	[7:0]	axi_arlen;
wire			axi_arvalid;
wire			axi_rready;
wire			axi_bready;


//==========================================================================
// AXI Interface
//==========================================================================
assign clk   = s00_axi_aclk;
assign rst_n = s00_axi_aresetn;

axi_if #(
	C_S_AXI_DATA_WIDTH,
	C_S_AXI_ADDR_WIDTH,
	VERSION
) axi_if (
	.axi_aclk    (s00_axi_aclk),
	.axi_aresetn (s00_axi_aresetn),
	.axi_awaddr  (s00_axi_awaddr),
	.axi_awprot  (s00_axi_awprot),
	.axi_awvalid (s00_axi_awvalid),
	.axi_awready (s00_axi_awready),
	.axi_wdata   (s00_axi_wdata),
	.axi_wstrb   (s00_axi_wstrb),
	.axi_wvalid  (s00_axi_wvalid),
	.axi_wready  (s00_axi_wready),
	.axi_bresp   (s00_axi_bresp),
	.axi_bvalid  (s00_axi_bvalid),
	.axi_bready  (s00_axi_bready),
	.axi_araddr  (s00_axi_araddr),
	.axi_arprot  (s00_axi_arprot),
	.axi_arvalid (s00_axi_arvalid),
	.axi_arready (s00_axi_arready),
	.axi_rdata   (s00_axi_rdata),
	.axi_rresp   (s00_axi_rresp),
	.axi_rvalid  (s00_axi_rvalid),
	.axi_rready  (s00_axi_rready),
	
	.led      (led),
	//.led      (),
	.slide_sw (8'b0),
	.push_sw  (5'b0),
	.ptn_sel  (ptn_sel),
	.bm_fcnt  (bm_fcnt),
	
	.addr   (ibus_addr),
	.cs     (ibus_cs),
	.wr     (ibus_wr),
	.wrdata (ibus_wrdata),
	.rddata (ibus_rddata),
	
	// Interrupt
	.arb_done (arb_done),
	.vsync    (vsync_sel),
	.intr     (intr)
);

assign ibus_cs_i2c  = (ibus_cs[1] & (ibus_addr[11:8] == 4'h1));
assign ibus_cs_frm  = (ibus_cs[1] & (ibus_addr[11:8] == 4'h2));
assign ibus_cs_mes  = (ibus_cs[1] & (ibus_addr[11:8] == 4'h3));
assign ibus_cs_sel  = (ibus_cs[1] & (ibus_addr[11:8] == 4'h4));
assign ibus_cs_grb  = (ibus_cs[1] & (ibus_addr[11:8] == 4'h5));
assign ibus_cs_xsbl = (ibus_cs[1] & (ibus_addr[11:8] == 4'h6));
assign ibus_cs_bm   = (ibus_cs[1] & (ibus_addr[11:8] == 4'h7));
assign ibus_cs_arb  = (ibus_cs[1] & (ibus_addr[11:8] == 4'h8));
assign ibus_cs_rect = (ibus_cs[1] & (ibus_addr[11:8] == 4'h9));

assign ibus_rddata = (
	ibus_rddata_i2c  |
	ibus_rddata_mes  |
	ibus_rddata_sel  |
	ibus_rddata_grb  |
	ibus_rddata_xsbl |
	ibus_rddata_bm   |
	ibus_rddata_arb  |
	ibus_rddata_rect |
    adapter_rddata
);


//==========================================================================
// Image Sensor Interface
//==========================================================================

// AX7010 adapter: original AXI control, BM, and DDR arbiter only.
// Existing board capture/display stay intact. CPU supplies packed X-Sobel
// stereo input and triggers one frame; no foreign calibration coefficients.
wire adapter_cs=ibus_cs[1] && ibus_addr[11:8]==4'ha;
reg adapter_start,adapter_busy;
reg [31:0] adapter_completed,adapter_errors;
reg [31:0] adapter_ar_bursts,adapter_rd_beats,adapter_wr_beats;
wire [31:0] bridge_status;
wire [31:0] adapter_rddata=!adapter_cs?32'b0:
    ibus_addr[7:2]==1?{31'b0,adapter_busy}:
    ibus_addr[7:2]==2?adapter_completed:
    ibus_addr[7:2]==3?adapter_errors:
    ibus_addr[7:2]==4?bridge_status:
    ibus_addr[7:2]==5?adapter_ar_bursts:
    ibus_addr[7:2]==6?adapter_rd_beats:
    ibus_addr[7:2]==7?adapter_wr_beats:32'b0;
always @(posedge clk) begin
 if(!rst_n) begin adapter_start<=0;adapter_busy<=0;adapter_completed<=0;adapter_errors<=0;
  adapter_ar_bursts<=0;adapter_rd_beats<=0;adapter_wr_beats<=0;end
 else begin
  adapter_start<=0;
  if(adapter_cs && ibus_wr && ibus_addr[7:2]==0 && ibus_wrdata[0] && bm_enb && !adapter_busy) begin
   adapter_start<=1;adapter_busy<=1;
  end
  if(bm_done)begin adapter_busy<=0;adapter_completed<=adapter_completed+1;end
  if(m00_axi_rvalid && m00_axi_rready && m00_axi_rresp!=0)adapter_errors<=adapter_errors|1;
  if(m00_axi_bvalid && m00_axi_bready && m00_axi_bresp!=0)adapter_errors<=adapter_errors|2;
  if(m00_axi_arvalid && m00_axi_arready)adapter_ar_bursts<=adapter_ar_bursts+1;
  if(m00_axi_rvalid && m00_axi_rready)adapter_rd_beats<=adapter_rd_beats+1;
  if(m00_axi_wvalid && m00_axi_wready)adapter_wr_beats<=adapter_wr_beats+1;
 end
end
assign ibus_rddata_i2c=0;
assign ibus_rddata_mes=0;
assign ibus_rddata_sel=0;
assign ibus_rddata_grb=0;
assign ibus_rddata_xsbl=0;
assign ibus_rddata_rect=0;
assign xsblw_req=0;assign xsblw_vout=0;assign xsblw_dout=0;assign xsblw_strb=4'hf;
assign rect_req=0;assign rect_vout=0;assign rect_dout=0;assign rect_strb=4'hf;
assign xsblr_req=0;assign xsblr_vout=0;assign xsblr_dout=0;
assign grb_req=0;assign grb_vout=0;assign grb_dout=0;
assign vsync_sel=0;assign xsbl_fcnt=0;
assign scl1=1;assign scl2=1;assign sda1=1'bz;assign sda2=1'bz;
assign drd_vin=drd_ack & arb_vout;
bm bm (
	// Global Control
	.rst_n (rst_n),
	.clk   (clk),
	
	// Internal Bus I/F
	.ibus_cs (ibus_cs_bm),
	.ibus_wr (ibus_wr),
	.ibus_addr_7_2 (ibus_addr[7:2]),
	.ibus_wrdata   (ibus_wrdata[31:0]),
	.ibus_rddata   (ibus_rddata_bm[31:0]),
	
	// Control
	.xsbl_done (xsbl_done),
	.bm_done   (bm_done),
	.xsbl_fcnt (xsbl_fcnt),
	.bm_fcnt   (bm_fcnt),
	.enb       (bm_enb),
	
	// DDR Read Arbiter I/F
	.drd_req  (drd_req),
	.drd_ack  (drd_ack),
	.drd_vin  (drd_vin),
	.drd_din  (arb_dout[31:0]),
	.drd_vout (drd_vout),
	.drd_dout (drd_dout[31:0]),
	
	// DDR Write Arbiter I/F
	.dwr_req  (dwr_req),
	.dwr_ack  (dwr_ack),
	.dwr_vout (dwr_vout),
	.dwr_dout (dwr_dout[31:0])
);


//==========================================================================
// DDR Arbiter
//==========================================================================
assign arb_req[5:0] = {
	xsblw_req,
	rect_req,
	dwr_req,
	drd_req,
	xsblr_req,
	grb_req
};
assign arb_vin = (
	xsblw_vout |
	rect_vout  |
	dwr_vout   |
	drd_vout   |
	xsblr_vout |
	grb_vout
);
assign arb_din[31:0] = (
	xsblw_dout[31:0] |
	rect_dout[31:0]  |
	dwr_dout[31:0]   |
	drd_dout[31:0]   |
	xsblr_dout[31:0] |
	grb_dout[31:0]
);
//assign arb_strb[3:0] = rect_strb[3:0];
assign arb_strb[3:0] = xsblw_strb & rect_strb[3:0]; // default high

assign xsblw_ack = arb_ack[5];
assign rect_ack  = arb_ack[4];
assign dwr_ack   = arb_ack[3];
assign drd_ack   = arb_ack[2];
assign xsblr_ack = arb_ack[1];
assign grb_ack   = arb_ack[0];

wire [31:0] legacy_awaddr,legacy_araddr,legacy_wdata;
wire [7:0] legacy_awlen,legacy_arlen;
wire [3:0] legacy_wstrb;
wire legacy_awvalid,legacy_awready,legacy_arvalid,legacy_arready;
wire legacy_wvalid,legacy_wready,legacy_wlast;
sdoira_axi_elastic elastic(
 .clk(clk),.rst_n(rst_n),
 .s_awaddr(legacy_awaddr),.s_awlen(legacy_awlen),.s_awvalid(legacy_awvalid),.s_awready(legacy_awready),
 .s_wdata(legacy_wdata),.s_wstrb(legacy_wstrb),.s_wlast(legacy_wlast),.s_wvalid(legacy_wvalid),.s_wready(legacy_wready),
 .s_araddr(legacy_araddr),.s_arlen(legacy_arlen),.s_arvalid(legacy_arvalid),.s_arready(legacy_arready),
 .m_awaddr(m00_axi_awaddr),.m_awlen(m00_axi_awlen),.m_awvalid(m00_axi_awvalid),.m_awready(m00_axi_awready),
 .m_wdata(m00_axi_wdata),.m_wstrb(m00_axi_wstrb),.m_wlast(m00_axi_wlast),.m_wvalid(m00_axi_wvalid),.m_wready(m00_axi_wready),
 .m_araddr(m00_axi_araddr),.m_arlen(m00_axi_arlen),.m_arvalid(m00_axi_arvalid),.m_arready(m00_axi_arready),
 .debug_status(bridge_status));
arb arb (
	// Global Control
	.rst_n (rst_n),
	.clk   (clk),
	
	// Internal Bus I/F
	.ibus_cs (ibus_cs_arb),
	.ibus_wr (ibus_wr),
	.ibus_addr_7_2 (ibus_addr[7:2]),
	.ibus_wrdata   (ibus_wrdata[31:0]),
	.ibus_rddata   (ibus_rddata_arb[31:0]),
	
	// Status
	.done (arb_done[5:0]),
	
	// FIFO I/F
	.req (arb_req),
	.ack (arb_ack),
	.valid_in  (arb_vin),
	.data_in   (arb_din),
	.strb_in   (arb_strb),
	.valid_out (arb_vout),
	.data_out  (arb_dout),
	
	// AXI I/F
	.axi_aclk    (m00_axi_aclk),
	.axi_awaddr  (legacy_awaddr),
	.axi_awlen   (legacy_awlen),
	.axi_awvalid (legacy_awvalid),
	.axi_awready (legacy_awready),
	.axi_wdata   (legacy_wdata),
	.axi_wstrb   (legacy_wstrb),
	.axi_wvalid  (legacy_wvalid),
	.axi_wready  (legacy_wready),
	.axi_wlast   (legacy_wlast),
	.axi_araddr  (legacy_araddr),
	.axi_arlen   (legacy_arlen),
	.axi_arvalid (legacy_arvalid),
	.axi_arready (legacy_arready),
	.axi_rdata   (m00_axi_rdata),
	.axi_rvalid  (m00_axi_rvalid),
	.axi_rready  (m00_axi_rready),
	.axi_rlast   (m00_axi_rlast),
	.axi_bvalid  (m00_axi_bvalid),
	.axi_bready  (m00_axi_bready)
);

// rename
assign grb_done  = arb_done[0];
assign rect_done = arb_done[4];
assign xsbl_done = adapter_start;
assign bm_done   = arb_done[3];

assign m00_axi_awid    =  1'b0;
assign m00_axi_awsize  =  3'd2; // 4 bytes
assign m00_axi_awburst =  2'd1; // INCR
assign m00_axi_awlock  =  1'b0;
assign m00_axi_awcache =  4'd2;
assign m00_axi_awprot  =  3'd0; // Data Secure Unprivileged
assign m00_axi_awqos   =  4'd0;
assign m00_axi_awuser  =  1'b0;
//assign m00_axi_wstrb   =  4'hF; // arb controlled
assign m00_axi_wuser   =  1'b0;
assign m00_axi_arid    =  1'b0;
assign m00_axi_arsize  =  3'd2; // 4 bytes
assign m00_axi_arburst =  2'd1; // INCR
assign m00_axi_arlock  =  1'b0;
assign m00_axi_arcache =  4'd2;
assign m00_axi_arprot  =  3'd0; // Data Secure Unprivileged
assign m00_axi_arqos   =  4'd0;
assign m00_axi_aruser  =  1'b0;


endmodule


