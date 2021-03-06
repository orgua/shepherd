# -*- coding: utf-8 -*-

"""
shepherd.commons
~~~~~
Defines details of the data exchange protocol between PRU0 and the python code.
The various parameters need to be the same on both sides. Refer to the
corresponding implementation in `software/firmware/include/commons.h`

:copyright: (c) 2019 Networked Embedded Systems Lab, TU Dresden.
:license: MIT, see LICENSE for more details.
"""
MAX_GPIO_EVT_PER_BUFFER = 16384

MSG_BUF_FROM_HOST = 1
MSG_BUF_FROM_PRU = 2

MSG_DBG_ADC = 0xA0
MSG_DBG_DAC = 0xA1
MSG_DBG_GPI = 0xA2
MSG_DBG_PRINT = 0xA6

# TODO: currently handled and filtered in kernel, should be in _get_msg()
MSG_DEP_ERR_INCMPLT = 0xE3
MSG_DEP_ERR_INVLDCMD = 0xE4
MSG_DEP_ERR_NOFREEBUF = 0xE5
