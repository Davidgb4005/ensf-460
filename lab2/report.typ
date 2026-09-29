#import "@preview/codly:1.3.0": *
#import "@preview/codly-languages:0.1.1": *
#show: codly-init.with()
#codly(languages: codly-languages)

#set page( paper: "a4", numbering: "1 of 1", footer: context [ _ENSF 460,
  Shulich School of Engineering_ #h(1fr) #counter(page).display("1/1", both:
    true) ])

#set heading(numbering: "1.1")

#set text(font: "IBM Plex Sans")

#align(center)[
  #text(size: 14pt)[
    \ \ \ \ \ 
    *Names:* Dave Burgoin, Moyo Ogunjobi, Jacob Plourde \
    *Group \#*: 6 \
    *Course:* ENSF 460 - Embedded Software and Hardware Systems \
    *Assignment Number:* Assignment 2 Driver Project \
    *Date Submitted:* #datetime.today().display("[month repr:long] [day], [year]")
  ]
]

#pagebreak()
