# Configuration file for the Sphinx documentation builder.
#
# -- Path setup --------------------------------------------------------------
#
# If extensions (or modules to document with autodoc) are in another directory,
# add these directories to sys.path here. If the directory is relative to the
# documentation root, use os.path.abspath to make it absolute, like shown here.
#
import os
import sys

sys.path.insert(0, os.path.realpath("../software/python-package"))
sys.path.insert(0, os.path.realpath("../software/shepherd-herd"))


# -- Project information -----------------------------------------------------

project = "SHEPHERD"
project_copyright = "2019-2024, Networked Embedded Systems Lab, TU Dresden & TU Darmstadt"
author = "Kai Geissdoerfer, Ingmar Splitt"
release = "0.9.0"

# -- General configuration ---------------------------------------------------

# Add any Sphinx extension module names here, as strings. They can be
# extensions coming with Sphinx (named 'sphinx.ext.*') or your custom
# ones.
extensions = [
    "sphinx.ext.napoleon",
    "sphinx_click.ext",
    "sphinx.ext.mathjax",
    "sphinx_sitemap",
    "sphinx_rtd_theme",
    "myst_parser",
    "sphinx_design",
    "sphinx_copybutton",
    # "sphinxcontrib.typer",
    "sphinxcontrib.autodoc_pydantic",
]
# TODO: check other sphinx-plugins (mentioned in pipfile) & breathe

# Add any paths that contain templates here, relative to this directory.
templates_path = ["_templates"]

# List of patterns, relative to source directory, that match files and
# directories to ignore when looking for source files.
# This pattern also affects html_static_path and html_extra_path.
exclude_patterns = ["_build", "Thumbs.db", ".DS_Store"]
autodoc_mock_imports = ["dbus"]

myst_enable_extensions = ["colon_fence"]
myst_heading_anchors = 3

autodoc_pydantic_model_show_json = False
autodoc_pydantic_settings_show_json = False
autodoc_pydantic_model_show_config_summary = False
autodoc_pydantic_model_show_validator_summary = False
autodoc_pydantic_model_show_validator_members = False
autodoc_pydantic_model_show_field_summary = True
autodoc_pydantic_model_summary_list_order = "bysource"
autodoc_pydantic_model_member_order = "bysource"
autodoc_pydantic_field_list_validators = False

# -- Options for HTML output -------------------------------------------------

html_title = project
html_collapsible_definitions = True
html_copy_source = False

html_permalinks_icon = "<span>#</span>"

html_theme = "sphinx_rtd_theme"
html_theme_options = {
    "display_version": True,
}
github_url = "https://github.com/orgua/shepherd"

html_baseurl = "https://orgua.github.io/shepherd/"
html_extra_path = ["robots.txt"]
# Add any paths that contain custom static files (such as style sheets) here,
# relative to this directory. They are copied after the builtin static files,
# so a file named "default.css" will overwrite the builtin "default.css".

sitemap_url_scheme = "{link}"
