/* Error codes shared with the Nations BLE driver sources. */
#ifndef __N32WB452_DATA_FIFO_H__
#define __N32WB452_DATA_FIFO_H__

#define E_OK        (0)
#define E_MSGFULL   (-7)
#define E_NODATA    (-8)
#define E_NOTENOUGH (-9)
#define E_OVERFLOW  (-10)
#define E_ERRDATA   (-11)
#define E_WAIT      (-15)
#define E_PAR       (-33)

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#endif
