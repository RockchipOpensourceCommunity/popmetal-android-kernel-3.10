#ifndef _DT_BINDINGS_RKFB_H_
#define _DT_BINDINGS_RKFB_H_
#define GPIO 		0
#define REGULATOR	1

#define PRMRY		1		/*primary display device*/
#define EXTEND		2		/*extend display device*/

#define NO_DUAL		0
#define ONE_DUAL	1
#define DUAL		2

#define OUT_P888            0	//24bit screen,connect to lcdc D0~D23
#define OUT_P666            1	//18bit screen,connect to lcdc D0~D17
#define OUT_P565            2
#define OUT_S888x           4
#define OUT_CCIR656         6
#define OUT_S888            8
#define OUT_S888DUMY        12
#define OUT_P16BPP4         24
#define OUT_D888_P666       0x21	//18bit screen,connect to lcdc D2~D7, D10~D15, D18~D23
#define OUT_D888_P565       0x22

#define SCREEN_NULL        0
#define SCREEN_RGB	   1
#define SCREEN_LVDS	   2
#define SCREEN_DUAL_LVDS   3
#define SCREEN_MCU         4
#define SCREEN_TVOUT       5
#define SCREEN_HDMI        6
#define SCREEN_MIPI	   7
#define SCREEN_DUAL_MIPI   8
#define SCREEN_EDP         9
#define SCREEN_TVOUT_TEST  10

#define LVDS_8BIT_1     0
#define LVDS_8BIT_2     1
#define LVDS_8BIT_3     2
#define LVDS_6BIT       3

#define NO_MIRROR	0
#define X_MIRROR    	1
#define Y_MIRROR    	2
#define X_Y_MIRROR    	3
#define ROTATE_90	4
#define ROTATE_180	8
#define ROTATE_270	12

#define COLOR_RGB	0
#define COLOR_YCBCR	1

/* fb win map */
#define FB_DEFAULT_ORDER		0
#define FB0_WIN2_FB1_WIN1_FB2_WIN0	12
#define FB0_WIN1_FB1_WIN2_FB2_WIN0	21
#define FB0_WIN2_FB1_WIN0_FB2_WIN1	102
#define FB0_WIN0_FB1_WIN2_FB2_WIN1	120
#define FB0_WIN0_FB1_WIN1_FB2_WIN2	210
#define FB0_WIN1_FB1_WIN0_FB2_WIN2	201
#define FB0_WIN0_FB1_WIN1_FB2_WIN2_FB3_WIN3	3210

#define DISPLAY_POLICY_SDK	0
#define DISPLAY_POLICY_BOX	1

/*      	lvds connect config       
 *                                        
 *          	LVDS_8BIT_1    LVDS_8BIT_2     LVDS_8BIT_3     LVDS_6BIT
----------------------------------------------------------------------
	TX0	R0		R2		R2		R0
	TX1	R1		R3		R3		R1
	TX2	R2		R4		R4		R2
Y	TX3	R3		R5		R5		R3
0	TX4	R4		R6		R6		R4
	TX6	R5		R7		R7		R5	
	TX7	G0		G2		G2		G0
----------------------------------------------------------------------
	TX8	G1		G3		G3		G1
	TX9	G2		G4		G4		G2
Y	TX12   	G3		G5		G5		G3
1	TX13   	G4		G6		G6		G4
 	TX14   	G5		G7		G7		G5
	TX15   	B0		B2		B2		B0
	TX18   	B1		B3		B3		B1
----------------------------------------------------------------------
	TX19	B2		B4		B4		B2
	TX20   	B3		B5		B5		B3
	TX21   	B4		B6		B6		B4
Y	TX22   	B5		B7		B7		B5
2	TX24   	HSYNC		HSYNC		HSYNC		HSYNC
	TX25	VSYNC		VSYNC		VSYNC		VSYNC
	TX26	ENABLE		ENABLE		ENABLE		ENABLE
----------------------------------------------------------------------    
	TX27	R6		R0		GND		GND
	TX5	R7		R1		GND		GND
	TX10   	G6		G0		GND		GND
Y	TX11   	G7		G1		GND		GND
3	TX16   	B6		B0		GND		GND
	TX17   	B7		B1		GND		GND
	TX23   	RSVD		RSVD		RSVD		RSVD
----------------------------------------------------------------------
*/

#endif
