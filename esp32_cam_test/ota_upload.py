"""Thin wrapper — delegates to the ota_upload.py in the parent folder."""
import sys, os, runpy

parent_script = os.path.join(os.path.dirname(__file__), '..', 'ota_upload.py')
sys.exit(runpy.run_path(os.path.normpath(parent_script), run_name='__main__') and 0)
