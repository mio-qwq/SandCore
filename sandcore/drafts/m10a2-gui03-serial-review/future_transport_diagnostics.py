"""只供未来新driver读取诊断；不重连、不清错、不改变任何验收状态。"""
import time


def error_chain(error):
    rows, seen = [], set()
    while error is not None and id(error) not in seen:
        seen.add(id(error))
        rows.append(dict(type=type(error).__name__, repr=repr(error),
                         message=str(error), winerror=getattr(error, 'winerror', None),
                         errno=getattr(error, 'errno', None)))
        error = error.__cause__ or (None if error.__suppress_context__ else error.__context__)
    return rows


def transport_snapshot(guest, error=None):
    """线程状态并非VM一致快照；不取request lock，避免等待阻塞中的发送。"""
    client = guest.client
    with client.condition:
        failure = client.failure
        pending, sequence = client.pending, client.sequence
        response_bytes = None if client.response is None else len(client.response)
    return dict(scope='HOST_PYTHON_STATE_ONLY_NOT_COHERENT_VM_OR_RECOVERY',
                seconds=time.monotonic(), raised=error_chain(error),
                client_failure=error_chain(failure),
                guest_errors=[error_chain(value) for value in list(guest.errors)],
                reader_alive=guest.reader.is_alive(),
                heart_alive=None if guest.heart is None else guest.heart.is_alive(),
                stop=guest.stop.is_set(), sequence=sequence, pending=pending,
                response_bytes=response_bytes,
                console_bytes=len(guest.console), debug_bytes=len(guest.debug),
                recovery_attempted=False, acceptance_state_modified=False)
