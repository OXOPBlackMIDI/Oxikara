from boask import route, use, run_server, html_templates

@route("/")
def index(handler):
    return html_templates.render(
        "index.html"
    )


if __name__ == "__main__":
    run_server()
