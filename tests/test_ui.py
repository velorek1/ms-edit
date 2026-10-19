"""Exercise the actual POSIX executable through a pseudo-terminal.

These tests verify user commands and saved bytes, not DOS BIOS compatibility.
"""
import fcntl
import os
from pathlib import Path
import pty
import select
import struct
import signal
import re
import subprocess
import tempfile
import termios
import time
import unittest

ROOT=Path(__file__).resolve().parents[1]


class Session:
    def __init__(self,*args):
        self.master,slave=pty.openpty()
        fcntl.ioctl(slave,termios.TIOCSWINSZ,struct.pack('HHHH',25,80,0,0))
        self.process=subprocess.Popen([str(ROOT/'editr'),*map(str,args)],
            stdin=slave,stdout=slave,stderr=slave,cwd=ROOT,close_fds=True,
            env={**os.environ,'TERM':'xterm'})
        os.close(slave)
        self.output=b''
        self.wait_for(b'F1=Help')

    def drain(self):
        while select.select([self.master],[],[],0)[0]:
            try:chunk=os.read(self.master,65536)
            except OSError:break
            if not chunk:break
            self.output+=chunk

    def wait(self,predicate,seconds=5):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            self.drain()
            if predicate():return
            select.select([self.master],[],[],0.01)
        raise AssertionError('UI timeout, tail: '+repr(self.output[-500:]))

    def wait_for(self,s,after=0):
        self.wait(lambda:s in self.output[after:])

    def send(self,data,expect=None):
        start=len(self.output)
        os.write(self.master,data)
        if expect:self.wait_for(expect,start)

    def file_is(self,path,expected):
        def matches():
            try:return path.read_bytes()==expected
            except FileNotFoundError:return False  # rename transaction in progress
        self.wait(matches)

    def exit(self):
        self.send(b'\x1bx')
        self.wait(lambda:self.process.poll() is not None)
        if self.process.returncode!=0:
            raise AssertionError('Editor exit '+str(self.process.returncode))

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
        self.process.wait(timeout=5)
        os.close(self.master)


class UITests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='ui-',dir=ROOT/'build')
        self.directory=Path(self.tmp.name)
        self.session=None

    def tearDown(self):
        if self.session:self.session.close()
        self.tmp.cleanup()

    def launch(self,*args):
        self.session=Session(*args)
        return self.session

    def test_escape_exit_save_cancel_and_discard(self):
        a=self.directory/'ESC.TXT';a.write_bytes(b'old')
        s=self.launch(a)
        s.send(b'new\x1b',b'Save file')
        s.send(b'\x1b',b'\x1b[?25h')  # Escape cancels confirmation.
        self.assertIsNone(s.process.poll())
        self.assertEqual(a.read_bytes(),b'old')
        s.send(b'\x1b',b'Save file')
        s.send(b'y')
        s.wait(lambda:s.process.poll() is not None)
        self.assertEqual(s.process.returncode,0)
        self.assertEqual(a.read_bytes(),b'newold\r\n')

    def test_escape_discard_and_clean_exit(self):
        a=self.directory/'DISCARD.TXT';a.write_bytes(b'old')
        s=self.launch(a);s.send(b'new\x1b',b'Save file');s.send(b'n')
        s.wait(lambda:s.process.poll() is not None)
        self.assertEqual(a.read_bytes(),b'old')
        s.close();self.session=None
        s=self.launch(a);s.send(b'\x1b')
        s.wait(lambda:s.process.poll() is not None)
        self.assertEqual(s.process.returncode,0)

    def test_help_from_menu_and_selector_returns_without_exiting(self):
        a=self.directory/'HELP.TXT';a.write_bytes(b'abc')
        s=self.launch(a)
        s.send(b'\x1bhc',b'Help - MS-EDIT Linux')
        s.wait_for(b'No undo/redo.')  # Wait for the body, not only the title.
        self.assertIn(b'Ctrl+Q,F',s.output)
        self.assertNotIn(b'EDIT.HLP',s.output)
        s.send(b'\x1b',b'\x1b[?25h')
        self.assertIsNone(s.process.poll())
        s.send(b'\x1bfo',b'File Name:')
        s.send(b'\x1bOP',b'Help - MS-EDIT Linux')
        s.send(b'\x1b',b'File Name:')
        s.send(b'\x1b',b'Line:');s.exit()
        self.assertEqual(a.read_bytes(),b'abc')

    def test_edit_clipboard_multidoc_find_save(self):
        a=self.directory/'A.TXT';b=self.directory/'B.TXT'
        s=self.launch(a)
        s.send(b'alpha\rbeta\r\x1bfs')
        s.file_is(a,b'alpha\r\nbeta\r\n')
        s.send(b'\x1b[1;5H\x1b[1;6F\x03\x1bfn\x16\x1bfs',b'File Name:')
        s.send(str(b).encode()+b'\r')
        s.file_is(b,b'alpha\r\nbeta\r\n')
        s.send(b'\x1b1\x1b[1;5H\x11f',b'Find What:')
        s.send(b'beta\r',b'Match case?')
        s.send(b'n',b'Match whole word only?')
        s.send(b'nBETA\x1bfs')
        s.file_is(a,b'alpha\r\nBETA\r\n')
        self.assertEqual(b.read_bytes(),b'alpha\r\nbeta\r\n')
        s.send(b'\x1b[13~',b'No more matches found.')
        s.send(b'\r')
        s.exit()

    def test_startup_failure_cleans_large_document_backing(self):
        a=self.directory/'LARGE.BIN';a.write_bytes(b'x'*1048576)
        result=subprocess.run([str(ROOT/'editr'),'-70',str(a)],
            stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE,
            env={**os.environ,'TEMP':str(self.directory)},timeout=10)
        self.assertEqual(result.returncode,1)
        self.assertIn(b'interactive terminal',result.stderr)
        self.assertEqual(list(self.directory.glob('EV*.TMP')),[])

    def test_unicode_whole_word_search_avoids_inside_spanish_word(self):
        a=self.directory/'WORDS.TXT';a.write_bytes('año a'.encode())
        s=self.launch(a);s.send(b'\x11f',b'Find What:')
        s.send(b'a\r',b'Match case?');s.send(b'n',b'Match whole word only?')
        s.send(b'yZ\x1bfs');s.file_is(a,'año Z\r\n'.encode());s.exit()

    def test_bracketed_paste_is_literal_multiline_and_utf8(self):
        a=self.directory/'PASTE.TXT';s=self.launch(a)
        payload='Español: á ñ\nsegunda línea'.encode()+b'\x0f'
        s.send(b'\x1b[200~'+payload+b'\x1b[201~\x1bfs')
        s.file_is(a,payload.replace(b'\n',b'\r\n')+b'\r\n');s.exit()

    def test_xterm_key_variants_and_x10_mouse(self):
        a=self.directory/'XTERM.TXT';a.write_bytes(b'abc')
        s=self.launch(a)
        s.send(b'\x1bOFZ\x1b[1~Q\x1b[8~\x1b[241;1u\x1b[27;1;225~\x1bfs')
        s.file_is(a,'QabcZñá\r\n'.encode())
        # X10 press/release packets must never become document characters.
        s.send(b'\x1b[M'+bytes([32,34,35])+b'\x1b[M'+bytes([35,34,35])+b'\x1bfs')
        s.file_is(a,'QabcZñá\r\n'.encode());s.exit()

    def test_cp437_file_receives_spanish_character_without_conversion(self):
        a=self.directory/'LEGACY.TXT';a.write_bytes(b'caf\x82')
        s=self.launch(a);s.send(b'\x1b[F'+'ñ'.encode()+b'\x1bfs')
        s.file_is(a,b'caf\x82\xa4\r\n');s.exit()

    def test_utf8_spanish_input_navigation_and_filename(self):
        a=self.directory/'español.txt'
        s=self.launch()
        s.send('año áéíóú ü ¿Qué? ¡Sí!'.encode())
        s.send(b'\x1b[H\x1b[C\x1b[3~')  # delete ñ as one character
        s.send('ñ'.encode()+b'\x1bfs',b'File Name:')
        s.send(str(a).encode()+b'\r')
        s.file_is(a,'año áéíóú ü ¿Qué? ¡Sí!\r\n'.encode())
        self.assertIn('año'.encode(),s.output)
        s.exit()
        # Reopen UTF-8 and delete the last multibyte character with Backspace.
        self.session.close();self.session=None
        s=self.launch(a);s.send(b'\x1b[H\x1b[C\x1b[C\x7f\x1bfs')
        s.file_is(a,'ao áéíóú ü ¿Qué? ¡Sí!\r\n'.encode());s.exit()

    def test_terminal_resize_and_small_terminal_preserve_document(self):
        a=self.directory/'RESIZE.TXT';s=self.launch(a);s.send(b'abc')
        start=len(s.output)
        fcntl.ioctl(s.master,termios.TIOCSWINSZ,struct.pack('HHHH',40,120,0,0))
        os.kill(s.process.pid,signal.SIGWINCH)
        s.wait_for(b'\x1b[40;1H',start)
        s.send(b'd')
        start=len(s.output)
        fcntl.ioctl(s.master,termios.TIOCSWINSZ,struct.pack('HHHH',18,60,0,0))
        os.kill(s.process.pid,signal.SIGWINCH)
        s.wait_for(b'Resize terminal',start)
        start=len(s.output)
        fcntl.ioctl(s.master,termios.TIOCSWINSZ,struct.pack('HHHH',30,100,0,0))
        os.kill(s.process.pid,signal.SIGWINCH)
        s.wait_for(b'\x1b[30;1H',start)
        s.send(b'e\x1bfs');s.file_is(a,b'abcde\r\n');s.exit()

    def resize(self,s,columns,rows,expect):
        start=len(s.output)
        fcntl.ioctl(s.master,termios.TIOCSWINSZ,struct.pack('HHHH',rows,columns,0,0))
        os.kill(s.process.pid,signal.SIGWINCH)
        s.wait_for(expect,start)

    def test_minimum_75x24_and_save_confirmation_buttons(self):
        a=self.directory/'MINIMUM.TXT';a.write_bytes(b'old')
        s=self.launch(a)
        self.resize(s,75,24,b'\x1b[24;1H')
        s.send(b'X\x1bx',b'Save file')
        self.assertIn(b'Yes',s.output);self.assertIn(b'Cancel',s.output)
        # Left wraps Yes -> Cancel; Enter must preserve the dirty document.
        s.send(b'\x1b[D\r',b'\x1b[?25h')
        self.assertEqual(a.read_bytes(),b'old')
        s.send(b'\x1bx',b'Save file')
        # Shrink during a modal, then restore: buttons remain actionable.
        self.resize(s,74,24,b'Resize terminal')
        self.resize(s,75,24,b'Save file')
        s.send(b'\r');s.wait(lambda:s.process.poll() is not None)
        self.assertEqual(a.read_bytes(),b'Xold\r\n')

    def test_confirmation_mouse_and_no_button(self):
        a=self.directory/'MOUSECONF.TXT';a.write_bytes(b'old')
        s=self.launch(a);s.send(b'X\x1bx',b'Save file')
        # 80x25: Cancel at x=50..61, y=15 (zero based).
        s.send(b'\x1b[<0;55;16M\x1b[<0;55;16m',b'\x1b[?25h')
        self.assertEqual(a.read_bytes(),b'old')
        s.send(b'\x1bx',b'Save file')
        s.send(b'\x1b[C\r');s.wait(lambda:s.process.poll() is not None)
        self.assertEqual(a.read_bytes(),b'old')

    def test_prompt_and_help_buttons_at_minimum_size(self):
        a=self.directory/'PROMPT.TXT';a.write_bytes(b'abc')
        s=self.launch(a);self.resize(s,75,24,b'\x1b[24;1H')
        s.send(b'\x11f',b'Find What:')
        # Field -> Cancel with Shift+Tab; Space is an activation key.
        s.send(b'ignored\x1b[Z ',b'\x1b[?25h')
        s.send(b'\x1bOP',b'Help - MS-EDIT Linux')
        self.resize(s,100,30,b'Help - MS-EDIT Linux')
        # Centered Help OK at x=46..53, y=23.
        s.send(b'\x1b[<0;50;24M\x1b[<0;50;24m',b'\x1b[?25h')
        self.resize(s,75,24,b'\x1b[24;1H')
        s.send(b'\x1bfo',b'File Name:')
        s.send(b'\x1b',b'Line:');s.exit()
        self.assertEqual(a.read_bytes(),b'abc')

    def test_selector_horizontal_buttons_cancel_without_opening(self):
        a=self.directory/'BUTTONS.TXT';a.write_bytes(b'old')
        s=self.launch(a);s.send(b'\x1bfo',b'File Name:')
        # Name, files, directories, read-only, binary, OK; Right -> Cancel.
        s.send(b'\t\t\t\t\t\x1b[C\r')
        s.send(b'X\x1bfs');s.file_is(a,b'Xold\r\n');s.exit()

    def test_reopening_document_preserves_unsaved_changes(self):
        a=self.directory/'SAME.TXT';a.write_bytes(b'abc')
        s=self.launch(a);s.send(b'X')
        s.send(b'\x1bfo',b'File Name:');s.send(str(a).encode()+b'\r')
        s.send(b'Y\x1bfs');s.file_is(a,b'XYabc\r\n');s.exit()

    def test_original_control_key_navigation_and_literal_bytes(self):
        a=self.directory/'KEYS.TXT';a.write_bytes(b'abc def xyz')
        s=self.launch(a)
        s.send(b'\x1b[1;5F\x01\x13\x06Z\x1b[1;5H\x0e\x0f\x1bfs')
        s.file_is(a,b'\x0e\x0fabc def Zxyz\r\n');s.exit()

    def test_mouse_selection_menu_and_save_button(self):
        a=self.directory/'MOUSE.TXT';b=self.directory/'COPY.TXT'
        a.write_bytes(b'hello world')
        s=self.launch(a)
        # SGR mouse: press at column zero, drag five characters, release.
        s.send(b'\x1b[<0;2;3M\x1b[<32;7;3M\x1b[<0;7;3m\x03')
        # Open File by mouse and click New in the drop-down.
        s.send(b'\x1b[<0;4;1M\x1b[<0;4;1m')
        s.send(b'\x1b[<0;6;3M\x1b[<0;6;3m\x16\x1bfs',b'File Name:')
        s.send(str(b).encode()+b'\x1b[<0;23;22M\x1b[<0;23;22m')
        s.file_is(b,b'hello\r\n');s.exit()
        self.assertEqual(a.read_bytes(),b'hello world')

    def test_wildcard_startup_opens_multiple_documents(self):
        a=self.directory/'A.TXT';b=self.directory/'B.TXT'
        a.write_bytes(b'alpha');b.write_bytes(b'beta')
        s=self.launch(str(self.directory/'*.TXT'))
        s.send(b'B\x1bfs');s.file_is(b,b'Bbeta\r\n')
        s.send(b'\x1b1A\x1bfs');s.file_is(a,b'Aalpha\r\n')
        s.exit()

    def test_selector_filter_directory_and_readonly(self):
        a=self.directory/'A.TXT';b=self.directory/'B.TXT'
        sub=self.directory/'SUB';sub.mkdir();c=sub/'C.BIN'
        a.write_bytes(b'alpha');b.write_bytes(b'beta');c.write_bytes(b'abc')
        s=self.launch(a)
        s.send(b'\x1bfo',b'File Name:')
        s.send(str(self.directory/'*.TXT').encode()+b'\r')
        s.send(b'\t\x1b[B\rB\x1bfs');s.file_is(b,b'Bbeta\r\n')
        s.send(b'\x1bfo',b'File Name:')
        s.send(str(c).encode()+b'\x1br\r',b'Line:1')
        s.send(b'X',b'You cannot modify a read-only file.')
        s.send(b'\r');s.exit()
        self.assertEqual(c.read_bytes(),b'abc')
        self.assertEqual(a.read_bytes(),b'alpha')

    def test_readonly_protects_disk(self):
        a=self.directory/'READONLY.BIN';original=bytes(range(256))
        a.write_bytes(original)
        s=self.launch('-R','-16',a)
        s.send(b'X',b'You cannot modify a read-only file.')
        s.send(b'\r')
        s.exit()
        self.assertEqual(a.read_bytes(),original)

    def test_cancel_exit_and_crlf_backspace(self):
        a=self.directory/'C.TXT'
        s=self.launch(a)
        s.send(b'one\rtwo\x1bx',b'This file has not been saved yet.')
        # Modal frames hide the cursor; a NEW show-cursor sequence proves
        # the isolated Escape was consumed and the main frame has returned.
        # F1=Help also occurs in the modal backdrop and is not a safe barrier.
        s.send(b'\x1b',b'\x1b[?25h')
        s.send(b'\x1b[H\x7f\x1bfs')
        s.file_is(a,b'onetwo\r\n')
        s.exit()

    def test_split_shared_document_independent_cursors(self):
        a=self.directory/'VIEWS.TXT';a.write_bytes(b'a\r\nb\r\nc\r\n')
        s=self.launch(a)
        # Ctrl+F6 splits; F6 switches focus, leaving each cursor independent.
        s.send(b'\x1b[17;5~\x1b[BX\x1b[17~Y\x1b[17~Z\x1bfs')
        s.file_is(a,b'Ya\r\nXZb\r\nc\r\n')
        # Resize cancellation and closing the focused window preserve data.
        s.send(b'\x1b[19;5~',b'Use arrow keys to resize window')
        s.send(b'\x1b[A')
        s.send(b'\x1b',b'\x1b[?25h')
        s.send(b'\x1b[14;5~\x1bfs')
        s.file_is(a,b'Ya\r\nXZb\r\nc\r\n')
        s.exit()

    def test_nine_documents_reject_tenth_and_close_shared(self):
        s=self.launch()
        for unused in range(8):s.send(b'\x1bfn')
        s.send(b'\x1bfn',b'no more than 9 files')
        s.send(b'\r\x1b[17;5~')
        # Close document 9 while shown in both views; edit the surviving doc.
        s.send(b'\x1bfcQ\x1bfs',b'File Name:')
        a=self.directory/'EIGHT.TXT'
        s.send(str(a).encode()+b'\r');s.file_is(a,b'Q\r\n')
        s.exit()

    def test_reference_like_initial_screen(self):
        s=self.launch()
        self.assertIn(b'Untitled',s.output)
        self.assertIn(b'Line:1',s.output)
        # Save an exact ANSI capture for an optional recreation-only preview.
        (ROOT/'build/initial-screen.ansi').write_bytes(s.output)
        s.exit()

    def test_replace_all_whole_word_and_empty_file(self):
        a=self.directory/'REPLACE.TXT'
        a.write_bytes(b'cat concatenate cat\r\nCAT')
        s=self.launch(a)
        s.send(b'\x11a',b'Find What:')
        s.send(b'cat\r',b'Replace With:')
        s.send(b'catcat\r',b'Match case?')
        s.send(b'n',b'Match whole word only?')
        s.send(b'y',b'Replace all in document?')
        s.send(b'y',b'3 replacements.')
        s.send(b'\r\x1bfs')
        s.file_is(a,b'catcat concatenate catcat\r\ncatcat\r\n')
        s.send(b'\x1b[1;5H\x1b[1;6F\x1b[3~\x1bfs')
        s.file_is(a,b'')
        s.exit()


if __name__=='__main__':unittest.main()
