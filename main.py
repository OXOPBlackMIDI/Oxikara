from boask import route, use, run_server, html_templates

@route("/")
def index(handler):
    return html_templates.render(
        "index.html",
        title="Oxikara",
        description="""Oxikara is a modern MIDI Player made using C++ and GLFW, GLAW, and Vulkan 1.1 (for Kepler and newer GPUs).""",
    )


if __name__ == "__main__":
    run_server()
