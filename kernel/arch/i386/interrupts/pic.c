/*
*This file is part of Dan's Open Source Disk Operating System (DOSDOS).
*
*    DOSDOS is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    DOSDOS is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with DOSDOS.  If not, see <http://www.gnu.org/licenses/>.
*/


#define PIC1		0x20		/* IO base address for master PIC */
#define PIC2		0xA0		/* IO base address for slave PIC */
#define PIC1_COMMAND	PIC1
#define PIC1_DATA	(PIC1+1)
#define PIC2_COMMAND	PIC2
#define PIC2_DATA	(PIC2+1)

#define PIC_READ_IRR    0x0a            /* OCW3 irq ready next CMD read */
#define PIC_READ_ISR    0x0b            /* OCW3 irq service next CMD read */

#define PIC_EOI		0x20		/* End-of-interrupt command code */

#define ICW1_ICW4	0x01		/* ICW4 (not) needed */
#define ICW1_SINGLE	0x02		/* Single (cascade) mode */
#define ICW1_INTERVAL4	0x04		/* Call address interval 4 (8) */
#define ICW1_LEVEL	0x08		/* Level triggered (edge) mode */
#define ICW1_INIT	0x10		/* Initialization - required! */
 
#define ICW4_8086	0x01		/* 8086/88 (MCS-80/85) mode */
#define ICW4_AUTO	0x02		/* Auto (normal) EOI */
#define ICW4_BUF_SLAVE	0x08		/* Buffered mode/slave */
#define ICW4_BUF_MASTER	0x0C		/* Buffered mode/master */
#define ICW4_SFNM	0x10		/* Special fully nested (not) */

//If we get an IRQ7, reading the pic_get_isr, we should find bit 7 is set
#define EXPECTED_IRQ7_PIC1_ISR 0x0080
//If we get an IRQ15, reading the pic_get_isr, we should find bit 15 is set
#define EXPECTED_IRQ15_PIC2_ISR 0x8000

#include <kernel/interrupts/pic.h>

static int pic1_mapstart = 0;
static int pic2_mapstart = 8;

static uint64_t spurious_irq_count = 0;

void PIC_remap(int offset1, int offset2){

  pic1_mapstart = offset1;
  pic2_mapstart = offset2;
  
  unsigned char a1, a2;
  
  a1 = inb(PIC1_DATA);
  a2 = inb(PIC2_DATA);
  
  outb(PIC1_COMMAND, ICW1_INIT+ICW1_ICW4);
  io_wait();
  outb(PIC2_COMMAND, ICW1_INIT+ICW1_ICW4);
  io_wait();
  outb(PIC1_DATA, offset1);
  io_wait();
  outb(PIC2_DATA, offset2);
  io_wait();
  outb(PIC1_DATA, 4);
  io_wait();
  outb(PIC2_DATA, 2);
  io_wait();
  
  outb(PIC1_DATA, ICW4_8086);
  io_wait();
  outb(PIC2_DATA, ICW4_8086);
  io_wait();
  
  outb(PIC1_DATA, a1);
  outb(PIC2_DATA, a2);
}

void IRQ_set_mask(unsigned char IRQline) {
  uint16_t port;
  uint8_t value;
  
  if(IRQline < 8) {
    port = PIC1_DATA;
  } else {
    port = PIC2_DATA;
    IRQline -= 8;
  }
  value = inb(port) | (1 << IRQline);
  outb(port, value);        
}
 
void IRQ_clear_mask(unsigned char IRQline) {

  uint16_t port;
  uint8_t value;
 
  if(IRQline < 8) {
    port = PIC1_DATA;
  } else {
    port = PIC2_DATA;
    IRQline -= 8;
  }
  value = inb(port) & ~(1 << IRQline);
  outb(port, value);        
}

static inline uint16_t __pic_get_irq_reg(int ocw3)
{
    /* OCW3 to PIC CMD to get the register values.  PIC2 is chained, and
     * represents IRQs 8-15.  PIC1 is IRQs 0-7, with 2 being the chain */
    outb(PIC1_COMMAND, ocw3);
    outb(PIC2_COMMAND, ocw3);
    return (inb(PIC2_COMMAND) << 8) | inb(PIC1_COMMAND);
}
 
/* Returns the combined value of the cascaded PICs irq request register */
uint16_t pic_get_irr()
{
    return __pic_get_irq_reg(PIC_READ_IRR);
}
 
/* Returns the combined value of the cascaded PICs in-service register */
uint16_t pic_get_isr()
{
    return __pic_get_irq_reg(PIC_READ_ISR);
}

void enable_IRQ(){
  asm("sti");
}

static inline bool is_spurious_irq(uint8_t irq){
  
  uint16_t pic_isr = pic_get_isr();

  //Bitwise AND to check that the expected IRQ line is indeed raised
  if(irq == (pic1_mapstart + 7)){
    return (pic_isr & EXPECTED_IRQ7_PIC1_ISR);
  }else if(irq == (pic2_mapstart + 7)){
    return (pic_isr & EXPECTED_IRQ15_PIC2_ISR);
  }

  //The irq is not the lowest priority for either PIC, so we know it can't be spurious
  return false;
}

void PIC_sendEOI(uint8_t irq){

  bool irq_is_spurious = is_spurious_irq(irq);
  
  if((irq >= pic2_mapstart) && (irq < (pic2_mapstart + 8))){

    //If the IRQ is spurious, then dont send EOI to the slave PIC
    //but increment the spurious IRQ counter to keep track
    if(!irq_is_spurious)
      outb(PIC2_COMMAND, PIC_EOI);
    else
      ++spurious_irq_count;

    //Even if it is a spurious IRQ, we still send EOI to the master PIC in this case
    outb(PIC1_COMMAND, PIC_EOI);
    
  }else if ((irq >= pic1_mapstart) && (irq < (pic1_mapstart + 8))){
    //If it is a spurious IRQ in this case, we send no EOI anywhere
    //but we do increment the spurious IRQ count
    if(!irq_is_spurious)
      outb(PIC1_COMMAND, PIC_EOI);
    else
      ++spurious_irq_count;   
  }
}

uint64_t num_spurious_IRQs(){
  return spurious_irq_count;
}
