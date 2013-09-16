/*****************************************************************************/
/*  LibreDWG - free implementation of the DWG file format                    */
/*                                                                           */
/*  Copyright (C) 2009 Free Software Foundation, Inc.                        */
/*                                                                           */
/*  This library is free software, licensed under the terms of the GNU       */
/*  General Public License as published by the Free Software Foundation,     */
/*  either version 3 of the License, or (at your option) any later version.  */
/*  You should have received a copy of the GNU General Public License        */
/*  along with this program.  If not, see <http://www.gnu.org/licenses/>.    */
/*****************************************************************************/

/**
 *     \file       section_locate.c
 *     \brief      Section locating functions
 *     \author     written by Felipe Castro
 *     \author     modified by Felipe Corrêa da Silva Sances
 *     \author     modified by Rodrigo Rodrigues da Silva
 *     \author     modified by Till Heuschmann
 *     \version    
 *     \copyright  GNU General Public License (version 3 or later)
 */

#include "compress_decompress.h"
#include "section_locate.h"
#include "logging.h"

/** Read R13-R15 Object Map */
void
read_R13_R15_section_locate(Bit_Chain *dat, Dwg_Data *dwg)
{
  uint8_t ckr, ckr2;
  int i;

  dwg->header.num_sections = bit_read_RL(dat);

  /* why do we have this limit to only 6 sections?
   *  It seems to be a bug, so I'll comment it out and will add dynamic
   *  allocation of the sections vector.
   *  OpenDWG spec speaks of 6 possible values for the record number
   *  Maybe the original libdwg author got confused about that. */
  /*
   if (dwg->header.num_sections > 6)
   dwg->header.num_sections = 6;
   */

  dwg->header.section = (Dwg_Section*) malloc(sizeof (Dwg_Section)
                         * dwg->header.num_sections);

  for (i = 0; i < dwg->header.num_sections; i++)
    {
      dwg->header.section[i].number  = bit_read_RC(dat);
      dwg->header.section[i].address = bit_read_RL(dat);
      dwg->header.section[i].size    = bit_read_RL(dat);
    }

  /* Check CRC on */
  ckr  = bit_ckr8(0xc0c1, dat->chain, dat->byte);
  ckr2 = bit_read_RS(dat);

  if (ckr != ckr2)
    {
      printf("header crc todo ckr:%x ckr2:%x \n", ckr, ckr2);
      return 1;
    }

  if (bit_search_sentinel(dat, dwg_sentinel(DWG_SENTINEL_HEADER_END)))
    LOG_TRACE("\n HEADER (end): %8X \n", (unsigned int) dat->byte)
}

/** Read R2004 Section Map */
/* The Section Map is a vector of number, size, and address triples used
 * to locate the sections in the file.
 */
void
read_R2004_section_map(Bit_Chain *dat, Dwg_Data *dwg, uint32_t comp_data_size,
                       uint32_t decomp_data_size)
{
  char *decomp, *ptr;
  int section_address, bytes_remaining, i;

  dwg->header.num_sections = 0;
  dwg->header.section      = 0;

  // allocate memory to hold decompressed data
  decomp = (char *) malloc(decomp_data_size * sizeof (char));

  if (decomp == 0)
    return;                // No memory

  decompress_R2004_section(dat, decomp, comp_data_size);
  LOG_TRACE("\n 2004 Section Map fields \n")

  section_address = 0x100;  // starting address
  i = 0;
  bytes_remaining = decomp_data_size;
  ptr = decomp;
  dwg->header.num_sections = 0;

  while(bytes_remaining)
  {
    if (dwg->header.num_sections == 0)
      dwg->header.section = (Dwg_Section*) malloc(sizeof (Dwg_Section));
    else
      dwg->header.section = (Dwg_Section*) realloc(dwg->header.section,
                             sizeof (Dwg_Section) 
                             * (dwg->header.num_sections+1));

    dwg->header.section[i].number  = *((int*) ptr);
    dwg->header.section[i].size    = *((int*) ptr + 1);
    dwg->header.section[i].address = section_address;
    section_address += dwg->header.section[i].size;
    bytes_remaining -= 8;
    ptr += 8;

    LOG_TRACE("SectionNumber: %d \n", dwg->header.section[i].number)
    LOG_TRACE("SectionSize: %x \n  ", dwg->header.section[i].size)
    LOG_TRACE("SectionAddr: %x \n  ", dwg->header.section[i].address)

    if (dwg->header.section[i].number < 0)
      {
        dwg->header.section[i].parent = *((int*) ptr);
        dwg->header.section[i].left   = *((int*) ptr + 1);
        dwg->header.section[i].right  = *((int*) ptr + 2);
        dwg->header.section[i].x00    = *((int*) ptr + 3);
        bytes_remaining -= 16;
        ptr += 16;

        LOG_TRACE("Parent: %d \n", (int)dwg->header.section[i].parent)
        LOG_TRACE("Left: %d \n  ", (int)dwg->header.section[i].left)
        LOG_TRACE("Right: %d \n ", (int)dwg->header.section[i].right)
        LOG_TRACE("0x00: %d \n"  , (int)dwg->header.section[i].x00)
      }
    dwg->header.num_sections++;
    i++;
  }

  /* check CRC */
  uint32_t ckr, ckr2;

  ckr  = bit_ckr32(0xc0c1, dat->chain, dat->byte);
  ckr2 = bit_read_RL(dat);

  if (ckr != ckr2)
    {
      printf("Header CRC todo ckr: %x ckr2: %x \n\n", ckr, ckr2);
      return 1;
    }
  free(decomp);
}

r2007_section*
read_sections_map(Bit_Chain *dat, int64_t size_comp, int64_t size_uncomp,
                  int64_t correction)
{
  char *data, *ptr, *ptr_end;
  r2007_section *section, *sections = 0, *last_section = 0;
  int i;
  
  data = read_system_page(dat, size_comp, size_uncomp, correction);
  ptr  = data;
  ptr_end = data + size_uncomp;
  LOG_TRACE("\n System Section (Section Map) \n")
  
  while (ptr < ptr_end)
    {
      section = (r2007_section*) malloc(sizeof(r2007_section));
      bfr_read(section, &ptr, 64);
      LOG_TRACE("\n Section \n")
      LOG_TRACE("data size:   %jd\n", section->data_size)
      LOG_TRACE("max size:    %jd\n", section->max_size)
      LOG_TRACE("encryption:  %jd\n", section->encrypted)
      LOG_TRACE("hashcode:    %jd\n", section->hashcode)
      LOG_TRACE("name length: %jd\n", section->name_length)
      LOG_TRACE("unknown:     %jd\n", section->unknown)
      LOG_TRACE("encoding:    %jd\n", section->encoded)
      LOG_TRACE("num pages:   %jd\n", section->num_pages)      
      section->next  = 0;
      section->pages = 0;
    
      if (sections == 0)
        sections = last_section = section;
      else
        {
          last_section->next = section;
          last_section = section;
        } 
      if (ptr >= ptr_end)
        break;
    
      // Section Name
      section->name = bfr_read_string(&ptr);
      LOG_TRACE("Section name:  %ls \n", (DWGCHAR*) section->name)      
      section->pages = (r2007_section_page**) malloc((size_t) section->num_pages
                        * sizeof(r2007_section_page*));
    
      for (i = 0; i < section->num_pages; i++)
        {
          section->pages[i] = (r2007_section_page*)
                               malloc(sizeof(r2007_section_page));

          bfr_read(section->pages[i], &ptr, 56);
          LOG_TRACE("\n Page \n")
          LOG_TRACE(" offset:      %jd\n", section->pages[i]->offset);
          LOG_TRACE(" size:        %jd\n", section->pages[i]->size);
          LOG_TRACE(" id:          %jd\n", section->pages[i]->id);
          LOG_TRACE(" uncomp_size: %jd\n", section->pages[i]->uncomp_size);
          LOG_TRACE(" comp_size:   %jd\n", section->pages[i]->comp_size);
          LOG_TRACE(" checksum:    %jd\n", section->pages[i]->checksum);
          LOG_TRACE(" crc:         %jd\n", section->pages[i]->crc);
        }
    }
  free(data);
  return sections;
}

/* Lookup a section in the section map. The section is identified by its hashcode.
 */
r2007_section*
get_section(r2007_section *sections_map, int64_t hashcode)
{
  r2007_section *section = sections_map;
  
  while (section != NULL)
    {
      if (section->hashcode == hashcode)
        break;
      section = section->next;
    }
  return section;
}
