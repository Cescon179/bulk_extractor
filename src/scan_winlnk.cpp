/**
 * \addtogroup winlnk
 * @{
 */
/**
 * \file
 *
 * scan_winlnk finds windows LNK files
 *
 * Revision history:
 * - 2014-apr-30 slg - created 
 *
 * Resources:
 * - http://msdn.microsoft.com/en-us/library/dd871305.aspx
 * - http://www.forensicswiki.org/wiki/LNK
 */

#include "config.h"
#include "be13_api/bulk_extractor_i.h"

#if defined(HAVE_LIBLNK_H) && defined(HAVE_LIBBFIO_H) && defined(HAVE_LIBLNK) && defined(HAVE_LIBBFIO)
#include "liblnk.h"
#include "libbfio.h"
#define USE_LIBLNK
#else
#undef USE_LIBLNK
#endif

#include <iostream>
#include <fstream>
#include <string>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sstream>
#include <vector>

#include "utf8.h"
#include "dfxml/src/dfxml_writer.h"

// sbuf_stream.h needs integrated into another include file as is done with sbuf_h?
#include "sbuf_stream.h"

static int debug=0;
const size_t SMALLEST_LNK_FILE = 1024;  // correct?


/**
 * Scanner scan_winlnk scans and extracts windows lnk records.
 * It takes scanner_params and recursion_control_block as input.
 * The scanner_params structure includes the following:
 * \li scanner_params::phase identifying the scanner phase.
 * \li scanner_params.sbuf containing the buffer being scanned.
 * \li scanner_params.fs, which provides feature recorder feature_recorder
 * that scan_winlnk will write to.
 *
 * scan_winlnk iterates through each byte of sbuf
 */
extern "C"
void scan_winlnk(const class scanner_params &sp,const recursion_control_block &rcb)
{
    assert(sp.sp_version==scanner_params::CURRENT_SP_VERSION);
    if(sp.phase==scanner_params::PHASE_STARTUP){
        assert(sp.info->si_version==scanner_info::CURRENT_SI_VERSION);
        sp.info->name		= "winlnk";
        sp.info->author		= "Simson Garfinkel";
        sp.info->description	= "Search for Windows LNK files";
        sp.info->feature_names.insert("winlnk");
        debug = sp.info->config->debug;
        return;
    }
    if(sp.phase==scanner_params::PHASE_SCAN){
	
	// phase 1: set up the feature recorder and search for winlnk features
	const sbuf_t &sbuf = sp.sbuf;
	feature_recorder *winlnk_recorder = sp.fs.get_name("winlnk");

	// make sure that potential LNK file is large enough and has the correct signature
	if (sbuf.pagesize <= SMALLEST_LNK_FILE){
            return;
        }

        for (size_t p=0;p < sbuf.pagesize - SMALLEST_LNK_FILE; p++){
            if ( sbuf.get32u(p+0x00) == 0x0000004c &&
                 sbuf.get32u(p+0x04) == 0x00021401 &&
                 sbuf.get32u(p+0x08) == 0x00000000 &&
                 sbuf.get32u(p+0x0c) == 0x000000c0 &&
                 sbuf.get32u(p+0x10) == 0x46000000 ){

		dfxml_writer::strstrmap_t lnkmap;
                
                uint32_t LinkFlags      = sbuf.get32u(p+0x0014);
                bool     HasLinkTargetIDList = LinkFlags & (1 << 0);
                bool     HasLinkInfo        = LinkFlags  & (1 << 1);
                //bool     HasName            = LinkFlags  & (1 << 2);
                //bool     HasRelativePath    = LinkFlags  & (1 << 3);
                //bool     HasWorkingDir      = LinkFlags  & (1 << 4);
                //uint32_t FileAttributes = sbuf.get32u(p+0x0018);
                uint64_t CreationTime   = sbuf.get64u(p+0x001c);
                uint64_t AccessTime     = sbuf.get64u(p+0x0024);
                uint64_t WriteTime      = sbuf.get64u(p+0x002c);

                size_t loc = 0x004c;    // location of next section
                
                lnkmap["ctime"] = microsoftDateToISODate(CreationTime);
                lnkmap["atime"] = microsoftDateToISODate(AccessTime);
                lnkmap["wtime"] = microsoftDateToISODate(WriteTime);

                if (HasLinkTargetIDList ){
                    uint16_t IDListSize = sbuf.get16u(p+loc);
                    loc += IDListSize + 2;
                }

                std::string path("NOLINKINFO");
                if (HasLinkInfo ){
                    uint32_t LinkInfoSize       = sbuf.get32u(p+loc);
                    //uint32_t LinkInfoHeaderSize = sbuf.get32u(p+loc+4);
                    //uint32_t LinkInfoFlags      = sbuf.get32u(p+loc+8);
                    //uint32_t VolumeIDOffset      = sbuf.get32u(p+loc+12);
                    uint32_t LocalBasePathOffset = sbuf.get32u(p+loc+16);
                    //uint32_t CommonNetworkRelativeLinkOffset = sbuf.get32u(p+loc+20);
                    //uint32_t CommonPathSuffixOffset = sbuf.get32u(p+loc+20);

                    /* copy out the pathname */
                    path = "";
                    for(size_t i = p+loc+LocalBasePathOffset; i<sbuf.bufsize && sbuf[i]; i++){
                        path += sbuf[i];
                    }
                    lnkmap["path"] = path;
                    loc += LinkInfoSize + 2;
                }
                winlnk_recorder->write(sbuf.pos0+p,path,dfxml_writer::xmlmap(lnkmap,"lnk",""));
                p += loc;
            }
        }
    }
}