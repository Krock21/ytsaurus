"""
Python client for HTTP-interface of Transfer Manager.

Package supports `Transfer Manager API <https://wiki.yandex-team.ru/transfer-manager/>`_.

Be ready to catch :py:exc:`yt.wrapper.errors.YtError` after all commands!
"""

from .client import get_version, TransferManager
from .global_client import (add_task, add_tasks, add_tasks_from_src_dst_pairs, abort_task, restart_task,
                            get_task_info, get_tasks, get_backend_config, YT_TRANSFER_MANAGER_URL_ENV)

__version__ = get_version()